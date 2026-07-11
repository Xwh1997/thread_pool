#include <projecta/concurrency/thread_pool.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

namespace pc = projecta::concurrency;

TEST(ThreadPoolContract, RejectsInvalidConfiguration) {
    EXPECT_THROW((pc::ThreadPool{{.thread_count = 0, .queue_capacity = 1}}),
                 std::invalid_argument);
    EXPECT_THROW((pc::ThreadPool{{.thread_count = 1, .queue_capacity = 0}}),
                 std::invalid_argument);
}

TEST(ThreadPoolContract, SubmitReturnsTaskValue) {
    pc::ThreadPool pool{{.thread_count = 2, .queue_capacity = 8}};

    auto submitted = pool.submit([](int value) { return value * 2; }, 21);

    ASSERT_TRUE(submitted.has_value());
    EXPECT_EQ(std::move(submitted).value().get(), 42);
}

TEST(ThreadPoolContract, SupportsMoveOnlyCallableAndArgument) {
    pc::ThreadPool pool{{.thread_count = 1, .queue_capacity = 2}};
    auto argument = std::make_unique<int>(41);

    auto submitted = pool.submit(
        [captured = std::make_unique<int>(1)](std::unique_ptr<int> value) {
            return *captured + *value;
        },
        std::move(argument));

    ASSERT_TRUE(submitted.has_value());
    EXPECT_EQ(std::move(submitted).value().get(), 42);
}

TEST(ThreadPoolContract, TaskExceptionPropagatesFromGet) {
    pc::ThreadPool pool{{.thread_count = 1, .queue_capacity = 2}};

    auto submitted = pool.submit([]() -> int {
        throw std::runtime_error("worker boom");
    });

    ASSERT_TRUE(submitted.has_value());
    EXPECT_THROW(std::move(submitted).value().get(), std::runtime_error);
}

TEST(ThreadPoolContract, EmptyTaskHandleReportsNoState) {
    pc::TaskHandle<int> empty;

    EXPECT_THROW(empty.get(), std::future_error);
}
