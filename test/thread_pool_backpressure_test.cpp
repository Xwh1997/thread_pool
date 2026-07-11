#include <projecta/concurrency/thread_pool.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <latch>

namespace pc = projecta::concurrency;
using namespace std::chrono_literals;

namespace {

struct SaturatedPool {
    pc::ThreadPool pool{{.thread_count = 1, .queue_capacity = 1}};
    std::promise<void> release;
    pc::TaskHandle<void> running;
    pc::TaskHandle<void> queued;

    SaturatedPool() {
        std::latch started{1};
        auto gate = release.get_future().share();
        auto first = pool.submit([&started, gate] {
            started.count_down();
            gate.wait();
        });
        if (!first.has_value()) {
            throw std::runtime_error("failed to submit running task");
        }
        running = std::move(first).value();
        started.wait();

        auto second = pool.submit([] {});
        if (!second.has_value()) {
            throw std::runtime_error("failed to submit queued task");
        }
        queued = std::move(second).value();
    }

    ~SaturatedPool() {
        release.set_value();
        running.wait();
        queued.wait();
    }
};

} // namespace

TEST(ThreadPoolBackpressure, ImmediateSubmitRejectsWhenQueueIsFull) {
    SaturatedPool saturated;

    auto result = saturated.pool.submit([] {});

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), pc::SubmitError::queue_full);
}

TEST(ThreadPoolBackpressure, TimedSubmitReturnsTimeoutWhenQueueStaysFull) {
    SaturatedPool saturated;

    auto result = saturated.pool.submit_for(20ms, [] {});

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), pc::SubmitError::timeout);
}
