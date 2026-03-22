#include <ThreadPool.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

// 在 -fsanitize=thread 下运行整套用例：TSAN 会在 stderr 报告数据竞争与潜在死锁；
// 以下用例刻意覆盖「worker 内再 submit 并阻塞等待」等易触发线程池自锁的场景，
// GTest 全部通过且进程以 0 退出，可作为「本轮构建下未发现 TSAN 所报问题」的工程化证据。

// 1. 正常提交：有返回值任务在 future 上拿到正确结果
TEST(ThreadPool, SubmitReturnsExpectedValue) {
    ThreadPool pool(4);
    auto a = pool.submit([](int x, int y) { return x * y + 1; }, 6, 7);
    EXPECT_EQ(a.get(), 43);
}

// 2. 正常提交：void 任务能跑完（通过同步原语确认执行过）
TEST(ThreadPool, SubmitVoidTaskRunsToCompletion) {
    ThreadPool pool(2);
    std::atomic<int> done{0};
    auto fut = pool.submit([&] { done.store(1); });
    fut.wait();
    EXPECT_EQ(done.load(), 1);
}

// 3. 任务内抛异常：在调用方通过 future.get() 捕获并校验异常类型与信息
TEST(ThreadPool, TaskExceptionPropagatesAndCanBeCaught) {
    ThreadPool pool(3);
    auto fut = pool.submit([] {
        throw std::runtime_error("worker boom");
    });
    try {
        fut.get();
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "worker boom");
    }
}

// 4. 高并发边界：多线程同时 submit，累计计数与任务数一致（无丢失）
TEST(ThreadPool, HighConcurrencyManySubmitsMatchAtomicCount) {
    constexpr int kProducers = 16;
    constexpr int kPerProducer = 500;
    ThreadPool pool(8);
    std::atomic<int> counter{0};
    std::vector<std::thread> producers;
    producers.reserve(kProducers);

    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&] {
            std::vector<std::future<void>> futs;
            futs.reserve(kPerProducer);
            for (int i = 0; i < kPerProducer; ++i) {
                futs.push_back(pool.submit([&] { counter.fetch_add(1, std::memory_order_relaxed); }));
            }
            for (auto& f : futs) {
                f.get();
            }
        });
    }
    for (auto& t : producers) {
        t.join();
    }
    EXPECT_EQ(counter.load(), kProducers * kPerProducer);
}

// 5. 高并发边界：极多短任务 + 少量线程，全部完成且结果可核对（队列积压 + worker 少）
TEST(ThreadPool, StressManyTasksFewThreadsSumMatches) {
    constexpr int kTasks = 20000;
    ThreadPool pool(2);
    std::vector<std::future<int>> futs;
    futs.reserve(kTasks);
    for (int i = 0; i < kTasks; ++i) {
        futs.push_back(pool.submit([i] { return i; }));
    }
    const auto t0 = std::chrono::steady_clock::now();
    long long sum = 0;
    for (auto& f : futs) {
        sum += f.get();
    }
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_EQ(sum, static_cast<long long>(kTasks - 1) * kTasks / 2);
    // ASAN + Debug 可能较慢，留足余量避免 CI 抖动误杀
    EXPECT_LT(elapsed, std::chrono::seconds(120));
}

// --- TSAN / 死锁相关：worker 内 nested submit + get（单 worker 池会真死锁，故至少 2 worker）---
TEST(ThreadPool, NoDeadlockNestedSubmitFromWorker) {
    ThreadPool pool(4);
    auto outer = pool.submit([&pool] {
        auto inner = pool.submit([] { return 42; });
        return inner.get();
    });
    EXPECT_EQ(outer.get(), 42);
}

TEST(ThreadPool, NoDeadlockConcurrentNestedSubmits) {
    constexpr int kOuters = 32;
    ThreadPool pool(8);
    std::vector<std::future<int>> outers;
    outers.reserve(kOuters);
    for (int i = 0; i < kOuters; ++i) {
        outers.push_back(pool.submit([&pool, i] {
            std::vector<std::future<int>> inners;
            inners.reserve(5);
            for (int j = 0; j < 5; ++j) {
                inners.push_back(pool.submit([i, j] { return i + j; }));
            }
            int sum = 0;
            for (auto& f : inners) {
                sum += f.get();
            }
            return sum;
        }));
    }
    int total = 0;
    for (auto& f : outers) {
        total += f.get();
    }
    // sum_i sum_j (i+j) = sum_i (5*i + 10) = 5 * (0+...+31) + 32*10
    const int expected = 5 * (kOuters - 1) * kOuters / 2 + 10 * kOuters;
    EXPECT_EQ(total, expected);
}

TEST(ThreadPool, NoDeadlockManyThreadsSubmitAndWaitEachFuture) {
    ThreadPool pool(6);
    std::promise<void> start;
    auto go = start.get_future();
    constexpr int kThreads = 12;
    constexpr int kPerThread = 50;
    std::atomic<int> done{0};
    std::vector<std::thread> clients;
    clients.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        clients.emplace_back([&] {
            go.wait();
            for (int i = 0; i < kPerThread; ++i) {
                pool.submit([&done] { done.fetch_add(1, std::memory_order_relaxed); }).wait();
            }
        });
    }
    start.set_value();
    for (auto& c : clients) {
        c.join();
    }
    EXPECT_EQ(done.load(), kThreads * kPerThread);
}
