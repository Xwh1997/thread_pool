#include <projecta/concurrency/thread_pool.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace pc = projecta::concurrency;

namespace {

class CountingObserver final : public pc::Observer {
public:
    void on_worker_start(std::size_t) noexcept override { worker_starts.fetch_add(1); }
    void on_worker_stop(std::size_t) noexcept override { worker_stops.fetch_add(1); }
    void on_task_accepted(std::uint64_t) noexcept override { accepted.fetch_add(1); }
    void on_task_started(std::uint64_t, std::size_t) noexcept override { started.fetch_add(1); }
    void on_task_finished(std::uint64_t, pc::TaskOutcome outcome) noexcept override {
        if (outcome == pc::TaskOutcome::succeeded) {
            succeeded.fetch_add(1);
        } else if (outcome == pc::TaskOutcome::failed) {
            failed.fetch_add(1);
        }
    }

    std::atomic<int> worker_starts{0};
    std::atomic<int> worker_stops{0};
    std::atomic<int> accepted{0};
    std::atomic<int> started{0};
    std::atomic<int> succeeded{0};
    std::atomic<int> failed{0};
};

} // namespace

TEST(ThreadPoolObservability, MetricsAndObserverReflectTaskOutcomes) {
    auto observer = std::make_shared<CountingObserver>();
    pc::ThreadPool pool{{.thread_count = 2, .queue_capacity = 4, .observer = observer}};

    auto success = pool.submit([] { return 42; });
    auto failure = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    ASSERT_TRUE(success.has_value());
    ASSERT_TRUE(failure.has_value());
    EXPECT_EQ(std::move(success).value().get(), 42);
    EXPECT_THROW(std::move(failure).value().get(), std::runtime_error);
    pool.wait();

    const auto metrics = pool.metrics();
    EXPECT_EQ(metrics.accepted, 2U);
    EXPECT_EQ(metrics.started, 2U);
    EXPECT_EQ(metrics.completed, 1U);
    EXPECT_EQ(metrics.failed, 1U);
    EXPECT_EQ(metrics.queued_now, 0U);
    EXPECT_EQ(metrics.running_now, 0U);
    EXPECT_EQ(observer->worker_starts.load(), 2);
    EXPECT_EQ(observer->worker_stops.load(), 2);
    EXPECT_EQ(observer->accepted.load(), 2);
    EXPECT_EQ(observer->started.load(), 2);
    EXPECT_EQ(observer->succeeded.load(), 1);
    EXPECT_EQ(observer->failed.load(), 1);
}
