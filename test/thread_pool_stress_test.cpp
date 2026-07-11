#include <projecta/concurrency/thread_pool.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <latch>
#include <thread>
#include <vector>

namespace pc = projecta::concurrency;
using namespace std::chrono_literals;

TEST(ThreadPoolStress, ManyProducersExecuteEveryAcceptedTaskExactlyOnce) {
    constexpr int producer_count = 8;
    constexpr int tasks_per_producer = 200;
    pc::ThreadPool pool{{.thread_count = 4, .queue_capacity = 64}};
    std::atomic<int> executions{0};
    std::latch start{1};
    std::vector<std::thread> producers;

    for (int producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&] {
            start.wait();
            std::vector<pc::TaskHandle<void>> handles;
            handles.reserve(tasks_per_producer);
            for (int task = 0; task < tasks_per_producer; ++task) {
                auto submitted = pool.submit_for(1s, [&] { executions.fetch_add(1); });
                ASSERT_TRUE(submitted.has_value());
                handles.push_back(std::move(submitted).value());
            }
            for (auto& handle : handles) {
                handle.get();
            }
        });
    }

    start.count_down();
    for (auto& producer : producers) {
        producer.join();
    }
    pool.wait();

    EXPECT_EQ(executions.load(), producer_count * tasks_per_producer);
    const auto metrics = pool.metrics();
    EXPECT_EQ(metrics.accepted, static_cast<std::uint64_t>(executions.load()));
    EXPECT_EQ(metrics.accepted, metrics.completed + metrics.failed + metrics.cancelled);
}

TEST(ThreadPoolStress, ConcurrentShutdownLeavesEveryAcceptedHandleTerminal) {
    pc::ThreadPool pool{{.thread_count = 4, .queue_capacity = 128}};
    std::atomic<int> attempts{0};
    std::latch start{1};
    std::vector<std::thread> producers;

    for (int producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&] {
            start.wait();
            while (true) {
                auto submitted = pool.submit([] {});
                attempts.fetch_add(1);
                if (!submitted.has_value()) {
                    if (submitted.error() == pc::SubmitError::queue_full) {
                        std::this_thread::yield();
                        continue;
                    }
                    EXPECT_EQ(submitted.error(), pc::SubmitError::shutting_down);
                    return;
                }
            }
        });
    }

    start.count_down();
    while (attempts.load() < 256) {
        std::this_thread::yield();
    }
    pool.shutdown(pc::ShutdownMode::cancel_pending);
    for (auto& producer : producers) {
        producer.join();
    }
    pool.wait();

    const auto metrics = pool.metrics();
    EXPECT_EQ(metrics.queued_now, 0U);
    EXPECT_EQ(metrics.running_now, 0U);
    EXPECT_EQ(metrics.accepted, metrics.completed + metrics.failed + metrics.cancelled);
}
