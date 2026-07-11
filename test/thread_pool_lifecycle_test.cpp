#include <projecta/concurrency/thread_pool.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <latch>
#include <memory>
#include <stop_token>
#include <thread>

namespace pc = projecta::concurrency;
using namespace std::chrono_literals;

TEST(ThreadPoolLifecycle, DrainRunsAcceptedTasksAndRejectsNewSubmissions) {
    pc::ThreadPool pool{{.thread_count = 1, .queue_capacity = 2}};
    std::promise<void> release;
    std::latch started{1};
    auto gate = release.get_future().share();
    std::atomic<int> completed{0};

    auto running = pool.submit([&] {
        started.count_down();
        gate.wait();
        completed.fetch_add(1);
    });
    ASSERT_TRUE(running.has_value());
    started.wait();
    auto queued = pool.submit([&] { completed.fetch_add(1); });
    ASSERT_TRUE(queued.has_value());

    pool.shutdown(pc::ShutdownMode::drain);
    auto rejected = pool.submit([] {});
    release.set_value();
    pool.wait();

    EXPECT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error(), pc::SubmitError::shutting_down);
    EXPECT_EQ(completed.load(), 2);
    EXPECT_EQ(pool.state(), pc::PoolState::stopped);
}

TEST(ThreadPoolLifecycle, CancelPendingCancelsQueuedAndRequestsRunningTaskStop) {
    pc::ThreadPool pool{{.thread_count = 1, .queue_capacity = 2}};
    std::latch started{1};

    auto running = pool.submit_stoppable([&](std::stop_token token) {
        started.count_down();
        while (!token.stop_requested()) {
            std::this_thread::yield();
        }
        return 7;
    });
    ASSERT_TRUE(running.has_value());
    started.wait();

    auto pending = pool.submit([] { return 42; });
    ASSERT_TRUE(pending.has_value());

    pool.shutdown(pc::ShutdownMode::cancel_pending);
    pool.wait();

    EXPECT_EQ(std::move(running).value().get(), 7);
    EXPECT_THROW(std::move(pending).value().get(), pc::TaskCancelled);
    EXPECT_EQ(pool.state(), pc::PoolState::stopped);
}

TEST(ThreadPoolLifecycle, BlockingOnSamePoolTaskFailsFast) {
    pc::ThreadPool pool{{.thread_count = 1, .queue_capacity = 2}};

    auto outer = pool.submit([&pool] {
        auto inner = pool.submit([] { return 42; });
        if (!inner.has_value()) {
            throw std::runtime_error("inner submission failed");
        }
        return std::move(inner).value().get();
    });

    ASSERT_TRUE(outer.has_value());
    EXPECT_THROW(std::move(outer).value().get(), pc::WouldDeadlock);
}

TEST(ThreadPoolLifecycle, WaitForReportsTimeoutThenStopped) {
    pc::ThreadPool pool{{.thread_count = 1, .queue_capacity = 1}};
    std::promise<void> release;
    std::latch started{1};
    auto gate = release.get_future().share();
    auto task = pool.submit([&] {
        started.count_down();
        gate.wait();
    });
    ASSERT_TRUE(task.has_value());
    started.wait();

    EXPECT_EQ(pool.wait_for(10ms), pc::WaitStatus::timeout);
    release.set_value();
    EXPECT_EQ(pool.wait_for(1s), pc::WaitStatus::ready);
}

TEST(ThreadPoolLifecycle, DestructionFromOwnWorkerCompletesAsynchronously) {
    auto owner = std::make_unique<pc::ThreadPool>(
        pc::ThreadPoolOptions{.thread_count = 1, .queue_capacity = 1});
    auto* pool = owner.get();
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::promise<void> destroyed;
    auto destroyed_future = destroyed.get_future();

    auto submitted = pool->submit(
        [owner = std::move(owner), gate, &destroyed]() mutable {
            gate.wait();
            owner.reset();
            destroyed.set_value();
        });
    ASSERT_TRUE(submitted.has_value());
    auto handle = std::move(submitted).value();
    release.set_value();

    EXPECT_EQ(destroyed_future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(handle.wait_for(1s), pc::WaitStatus::ready);
    EXPECT_NO_THROW(handle.get());
}
