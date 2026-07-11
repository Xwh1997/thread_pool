#pragma once

#include <projecta/concurrency/export.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#if !defined(__cpp_lib_jthread) || __cpp_lib_jthread < 201911L
#error "ProjectA ThreadPool requires C++20 std::jthread and std::stop_token support"
#endif

namespace projecta::concurrency {

enum class SubmitError { queue_full, timeout, shutting_down };
enum class ShutdownMode { drain, cancel_pending };
enum class PoolState { running, draining, cancelling, stopped };
enum class WaitStatus { ready, timeout, would_deadlock };
enum class TaskOutcome { succeeded, failed, cancelled };

class TaskCancelled : public std::runtime_error {
public:
    TaskCancelled() : std::runtime_error("task cancelled before execution") {}
};

class WouldDeadlock : public std::logic_error {
public:
    WouldDeadlock() : std::logic_error("blocking wait from a worker of the same pool") {}
};

class Observer {
public:
    virtual ~Observer() = default;
    virtual void on_worker_start(std::size_t) noexcept {}
    virtual void on_worker_stop(std::size_t) noexcept {}
    virtual void on_task_accepted(std::uint64_t) noexcept {}
    virtual void on_task_started(std::uint64_t, std::size_t) noexcept {}
    virtual void on_task_finished(std::uint64_t, TaskOutcome) noexcept {}
    virtual void on_state_changed(PoolState, PoolState) noexcept {}
};

struct ThreadPoolOptions {
    std::size_t thread_count{};
    std::size_t queue_capacity{};
    std::shared_ptr<Observer> observer{};
};

struct MetricsSnapshot {
    std::size_t workers{};
    std::size_t queue_capacity{};
    std::size_t queued_now{};
    std::size_t running_now{};
    std::uint64_t accepted{};
    std::uint64_t started{};
    std::uint64_t completed{};
    std::uint64_t failed{};
    std::uint64_t cancelled{};
    std::uint64_t rejected_full{};
    std::uint64_t rejected_shutdown{};
    std::uint64_t timed_out{};
};

namespace detail {

struct PoolIdentity final {};

PROJECTA_THREAD_POOL_EXPORT const PoolIdentity* current_pool_identity() noexcept;

template<bool Stoppable, class Function, class... Args>
struct SubmissionResultType;

template<class Function, class... Args>
struct SubmissionResultType<false, Function, Args...> {
    using type = std::invoke_result_t<Function&&, Args&&...>;
};

template<class Function, class... Args>
struct SubmissionResultType<true, Function, Args...> {
    using type = std::invoke_result_t<Function&&, std::stop_token, Args&&...>;
};

class TaskBase {
public:
    virtual ~TaskBase() = default;
    virtual TaskOutcome run(std::stop_token token) noexcept = 0;
    virtual void cancel() noexcept = 0;
    void set_id(std::uint64_t id) noexcept { id_ = id; }
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }

private:
    std::uint64_t id_{};
};

template<class Function, class Tuple, class Result, bool Stoppable>
class TaskModel final : public TaskBase {
public:
    TaskModel(Function function, Tuple arguments)
        : function_(std::move(function)), arguments_(std::move(arguments)) {}

    std::future<Result> get_future() { return promise_.get_future(); }

    TaskOutcome run(std::stop_token token) noexcept override {
        try {
            if constexpr (std::is_void_v<Result>) {
                invoke(token);
                promise_.set_value();
            } else {
                promise_.set_value(invoke(token));
            }
            return TaskOutcome::succeeded;
        } catch (...) {
            promise_.set_exception(std::current_exception());
            return TaskOutcome::failed;
        }
    }

    void cancel() noexcept override {
        try {
            promise_.set_exception(std::make_exception_ptr(TaskCancelled{}));
        } catch (...) {
        }
    }

private:
    decltype(auto) invoke(std::stop_token token) {
        return std::apply(
            [this, token](auto&... args) -> decltype(auto) {
                if constexpr (Stoppable) {
                    return std::invoke(std::move(function_), token, std::move(args)...);
                } else {
                    return std::invoke(std::move(function_), std::move(args)...);
                }
            },
            arguments_);
    }

    Function function_;
    Tuple arguments_;
    std::promise<Result> promise_;
};

} // namespace detail

template<class Result>
class TaskHandle {
public:
    TaskHandle() = default;
    TaskHandle(TaskHandle&&) noexcept = default;
    TaskHandle& operator=(TaskHandle&&) noexcept = default;
    TaskHandle(const TaskHandle&) = delete;
    TaskHandle& operator=(const TaskHandle&) = delete;

    [[nodiscard]] bool valid() const noexcept { return future_.valid(); }

    [[nodiscard]] bool is_ready() const {
        return valid() && future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    void wait() const {
        throw_if_same_pool_would_block();
        future_.wait();
    }

    template<class Rep, class Period>
    WaitStatus wait_for(std::chrono::duration<Rep, Period> timeout) const {
        if (is_ready()) {
            return WaitStatus::ready;
        }
        if (timeout <= timeout.zero()) {
            return WaitStatus::timeout;
        }
        if (identity_ && detail::current_pool_identity() == identity_.get()) {
            return WaitStatus::would_deadlock;
        }
        return future_.wait_for(timeout) == std::future_status::ready
                   ? WaitStatus::ready
                   : WaitStatus::timeout;
    }

    Result get() {
        throw_if_same_pool_would_block();
        return future_.get();
    }

private:
    friend class ThreadPool;
    TaskHandle(std::future<Result> future, std::shared_ptr<const detail::PoolIdentity> identity)
        : future_(std::move(future)), identity_(std::move(identity)) {}

    void throw_if_same_pool_would_block() const {
        if (identity_ && !is_ready() && detail::current_pool_identity() == identity_.get()) {
            throw WouldDeadlock{};
        }
    }

    std::future<Result> future_;
    std::shared_ptr<const detail::PoolIdentity> identity_;
};

template<class Result>
class [[nodiscard]] SubmitResult {
public:
    SubmitResult(TaskHandle<Result> handle) : storage_(std::move(handle)) {}
    SubmitResult(SubmitError error) : storage_(error) {}

    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<TaskHandle<Result>>(storage_);
    }

    explicit operator bool() const noexcept { return has_value(); }

    TaskHandle<Result>& value() & { return std::get<TaskHandle<Result>>(storage_); }
    TaskHandle<Result>&& value() && { return std::get<TaskHandle<Result>>(std::move(storage_)); }
    [[nodiscard]] SubmitError error() const { return std::get<SubmitError>(storage_); }

private:
    std::variant<TaskHandle<Result>, SubmitError> storage_;
};

class PROJECTA_THREAD_POOL_EXPORT ThreadPool {
public:
    explicit ThreadPool(ThreadPoolOptions options);
    ~ThreadPool() noexcept;

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template<class F, class... Args>
    auto submit(F&& function, Args&&... args) {
        return make_submission<false>(std::forward<F>(function), std::forward<Args>(args)...);
    }

    template<class Rep, class Period, class F, class... Args>
    auto submit_for(std::chrono::duration<Rep, Period> timeout, F&& function, Args&&... args) {
        return make_timed_submission<false>(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout),
            std::forward<F>(function),
            std::forward<Args>(args)...);
    }

    template<class F, class... Args>
    auto submit_stoppable(F&& function, Args&&... args) {
        return make_submission<true>(std::forward<F>(function), std::forward<Args>(args)...);
    }

    template<class Rep, class Period, class F, class... Args>
    auto submit_stoppable_for(
        std::chrono::duration<Rep, Period> timeout, F&& function, Args&&... args) {
        return make_timed_submission<true>(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout),
            std::forward<F>(function),
            std::forward<Args>(args)...);
    }

    void shutdown(ShutdownMode mode = ShutdownMode::drain) noexcept;
    void wait();
    template<class Rep, class Period>
    WaitStatus wait_for(std::chrono::duration<Rep, Period> timeout) {
        return wait_for_impl(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout));
    }
    [[nodiscard]] PoolState state() const noexcept;
    [[nodiscard]] MetricsSnapshot metrics() const noexcept;

private:
    class Impl;

    template<bool Stoppable, class F, class... Args>
    auto make_submission(F&& function, Args&&... args) {
        using StoredFunction = std::decay_t<F>;
        using StoredTuple = std::tuple<std::decay_t<Args>...>;
        using Result = typename detail::SubmissionResultType<
            Stoppable, StoredFunction, std::decay_t<Args>...>::type;
        using Model = detail::TaskModel<StoredFunction, StoredTuple, Result, Stoppable>;

        auto task = std::make_unique<Model>(
            StoredFunction(std::forward<F>(function)),
            StoredTuple(std::forward<Args>(args)...));
        auto future = task->get_future();
        auto identity = identity_;
        if (const auto error = enqueue(std::move(task)); error.has_value()) {
            return SubmitResult<Result>{*error};
        }
        return SubmitResult<Result>{TaskHandle<Result>{std::move(future), std::move(identity)}};
    }

    template<bool Stoppable, class F, class... Args>
    auto make_timed_submission(
        std::chrono::steady_clock::duration timeout, F&& function, Args&&... args) {
        using StoredFunction = std::decay_t<F>;
        using StoredTuple = std::tuple<std::decay_t<Args>...>;
        using Result = typename detail::SubmissionResultType<
            Stoppable, StoredFunction, std::decay_t<Args>...>::type;
        using Model = detail::TaskModel<StoredFunction, StoredTuple, Result, Stoppable>;

        auto task = std::make_unique<Model>(
            StoredFunction(std::forward<F>(function)),
            StoredTuple(std::forward<Args>(args)...));
        auto future = task->get_future();
        auto identity = identity_;
        if (const auto error = enqueue_for(timeout, std::move(task)); error.has_value()) {
            return SubmitResult<Result>{*error};
        }
        return SubmitResult<Result>{TaskHandle<Result>{std::move(future), std::move(identity)}};
    }

    std::optional<SubmitError> enqueue(std::unique_ptr<detail::TaskBase> task);
    std::optional<SubmitError> enqueue_for(
        std::chrono::steady_clock::duration timeout,
        std::unique_ptr<detail::TaskBase> task);
    WaitStatus wait_for_impl(std::chrono::steady_clock::duration timeout);

    std::unique_ptr<Impl> impl_;
    std::shared_ptr<const detail::PoolIdentity> identity_;
};

} // namespace projecta::concurrency
