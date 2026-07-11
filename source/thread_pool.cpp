#include <projecta/concurrency/thread_pool.hpp>

#include <condition_variable>
#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace projecta::concurrency {
namespace {

thread_local const detail::PoolIdentity* tls_pool_identity = nullptr;

struct Core {
    explicit Core(ThreadPoolOptions value)
        : options(std::move(value)), identity(std::make_shared<detail::PoolIdentity>()) {}

    ThreadPoolOptions options;
    std::shared_ptr<const detail::PoolIdentity> identity;
    mutable std::mutex mutex;
    std::mutex thread_mutex;
    std::condition_variable not_empty;
    std::condition_variable not_full;
    std::condition_variable all_stopped;
    std::deque<std::unique_ptr<detail::TaskBase>> queue;
    PoolState state{PoolState::running};
    std::stop_source task_stop_source;
    std::size_t live_workers{};
    std::vector<std::jthread> workers;
    std::atomic<std::uint64_t> next_task_id{1};
    std::atomic<std::size_t> queued_now{0};
    std::atomic<std::size_t> running_now{0};
    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> started{0};
    std::atomic<std::uint64_t> completed{0};
    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> cancelled{0};
    std::atomic<std::uint64_t> rejected_full{0};
    std::atomic<std::uint64_t> rejected_shutdown{0};
    std::atomic<std::uint64_t> timed_out{0};
};

void worker_loop(const std::shared_ptr<Core>& core, std::size_t worker_index) noexcept {
    tls_pool_identity = core->identity.get();
    if (core->options.observer) {
        core->options.observer->on_worker_start(worker_index);
    }
    while (true) {
        std::unique_ptr<detail::TaskBase> task;
        {
            std::unique_lock lock(core->mutex);
            core->not_empty.wait(lock, [&] {
                return !core->queue.empty() || core->state != PoolState::running;
            });
            if (core->queue.empty()) {
                break;
            }
            task = std::move(core->queue.front());
            core->queue.pop_front();
            core->queued_now.fetch_sub(1, std::memory_order_relaxed);
            core->running_now.fetch_add(1, std::memory_order_relaxed);
            core->started.fetch_add(1, std::memory_order_relaxed);
        }
        core->not_full.notify_one();
        if (core->options.observer) {
            core->options.observer->on_task_started(task->id(), worker_index);
        }
        const auto outcome = task->run(core->task_stop_source.get_token());
        core->running_now.fetch_sub(1, std::memory_order_relaxed);
        if (outcome == TaskOutcome::succeeded) {
            core->completed.fetch_add(1, std::memory_order_relaxed);
        } else {
            core->failed.fetch_add(1, std::memory_order_relaxed);
        }
        if (core->options.observer) {
            core->options.observer->on_task_finished(task->id(), outcome);
        }
    }

    if (core->options.observer) {
        core->options.observer->on_worker_stop(worker_index);
    }
    tls_pool_identity = nullptr;
    PoolState previous = PoolState::stopped;
    bool changed = false;
    {
        std::lock_guard lock(core->mutex);
        --core->live_workers;
        if (core->live_workers == 0) {
            previous = core->state;
            core->state = PoolState::stopped;
            changed = previous != PoolState::stopped;
        }
    }
    if (changed && core->options.observer) {
        core->options.observer->on_state_changed(previous, PoolState::stopped);
    }
    core->all_stopped.notify_all();
}

} // namespace

namespace detail {

const PoolIdentity* current_pool_identity() noexcept {
    return tls_pool_identity;
}

} // namespace detail

class ThreadPool::Impl {
public:
    explicit Impl(ThreadPoolOptions options) : core(std::make_shared<Core>(std::move(options))) {}
    std::shared_ptr<Core> core;
};

ThreadPool::ThreadPool(ThreadPoolOptions options) {
    if (options.thread_count == 0 || options.queue_capacity == 0) {
        throw std::invalid_argument("thread_count and queue_capacity must be greater than zero");
    }

    impl_ = std::make_unique<Impl>(std::move(options));
    auto core = impl_->core;
    identity_ = core->identity;
    core->workers.reserve(core->options.thread_count);
    try {
        for (std::size_t index = 0; index < core->options.thread_count; ++index) {
            ++core->live_workers;
            try {
                core->workers.emplace_back([core, index] { worker_loop(core, index); });
            } catch (...) {
                --core->live_workers;
                throw;
            }
        }
    } catch (...) {
        {
            std::lock_guard lock(core->mutex);
            core->state = PoolState::cancelling;
        }
        core->not_empty.notify_all();
        core->not_full.notify_all();
        for (auto& worker : core->workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        throw;
    }
}

ThreadPool::~ThreadPool() noexcept {
    shutdown(ShutdownMode::drain);
    auto core = impl_->core;
    if (detail::current_pool_identity() == core->identity.get()) {
        std::lock_guard thread_lock(core->thread_mutex);
        for (auto& worker : core->workers) {
            if (worker.joinable()) {
                worker.detach();
            }
        }
        return;
    }
    try {
        wait();
    } catch (...) {
        std::terminate();
    }
}

std::optional<SubmitError> ThreadPool::enqueue(std::unique_ptr<detail::TaskBase> task) {
    auto core = impl_->core;
    std::uint64_t task_id{};
    {
        std::lock_guard lock(core->mutex);
        if (core->state != PoolState::running) {
            core->rejected_shutdown.fetch_add(1, std::memory_order_relaxed);
            return SubmitError::shutting_down;
        }
        if (core->queue.size() >= core->options.queue_capacity) {
            core->rejected_full.fetch_add(1, std::memory_order_relaxed);
            return SubmitError::queue_full;
        }
        task_id = core->next_task_id.fetch_add(1, std::memory_order_relaxed);
        task->set_id(task_id);
        core->queue.push_back(std::move(task));
        core->accepted.fetch_add(1, std::memory_order_relaxed);
        core->queued_now.fetch_add(1, std::memory_order_relaxed);
    }
    core->not_empty.notify_one();
    if (core->options.observer) {
        core->options.observer->on_task_accepted(task_id);
    }
    return std::nullopt;
}

std::optional<SubmitError> ThreadPool::enqueue_for(
    std::chrono::steady_clock::duration timeout,
    std::unique_ptr<detail::TaskBase> task) {
    auto core = impl_->core;
    std::unique_lock lock(core->mutex);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const bool ready = core->not_full.wait_until(lock, deadline, [&] {
        return core->state != PoolState::running ||
               core->queue.size() < core->options.queue_capacity;
    });
    if (core->state != PoolState::running) {
        core->rejected_shutdown.fetch_add(1, std::memory_order_relaxed);
        return SubmitError::shutting_down;
    }
    if (!ready) {
        core->timed_out.fetch_add(1, std::memory_order_relaxed);
        return SubmitError::timeout;
    }
    const auto task_id = core->next_task_id.fetch_add(1, std::memory_order_relaxed);
    task->set_id(task_id);
    core->queue.push_back(std::move(task));
    core->accepted.fetch_add(1, std::memory_order_relaxed);
    core->queued_now.fetch_add(1, std::memory_order_relaxed);
    lock.unlock();
    core->not_empty.notify_one();
    if (core->options.observer) {
        core->options.observer->on_task_accepted(task_id);
    }
    return std::nullopt;
}

void ThreadPool::shutdown(ShutdownMode mode) noexcept {
    auto core = impl_->core;
    std::deque<std::unique_ptr<detail::TaskBase>> cancelled;
    bool request_stop = false;
    PoolState previous = PoolState::stopped;
    PoolState next = PoolState::stopped;
    bool state_changed = false;
    {
        std::lock_guard lock(core->mutex);
        if (core->state == PoolState::stopped || core->state == PoolState::cancelling) {
            return;
        }
        previous = core->state;
        if (mode == ShutdownMode::cancel_pending) {
            core->state = PoolState::cancelling;
            cancelled.swap(core->queue);
            core->queued_now.fetch_sub(cancelled.size(), std::memory_order_relaxed);
            request_stop = true;
        } else if (core->state == PoolState::running) {
            core->state = PoolState::draining;
        }
        next = core->state;
        state_changed = previous != next;
    }
    core->not_empty.notify_all();
    core->not_full.notify_all();
    for (auto& task : cancelled) {
        task->cancel();
        core->cancelled.fetch_add(1, std::memory_order_relaxed);
        if (core->options.observer) {
            core->options.observer->on_task_finished(task->id(), TaskOutcome::cancelled);
        }
    }
    if (request_stop) {
        core->task_stop_source.request_stop();
    }
    if (state_changed && core->options.observer) {
        core->options.observer->on_state_changed(previous, next);
    }
}

void ThreadPool::wait() {
    auto core = impl_->core;
    if (detail::current_pool_identity() == core->identity.get()) {
        throw WouldDeadlock{};
    }
    shutdown(ShutdownMode::drain);
    {
        std::unique_lock lock(core->mutex);
        core->all_stopped.wait(lock, [&] { return core->live_workers == 0; });
    }
    std::lock_guard thread_lock(core->thread_mutex);
    for (auto& worker : core->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

WaitStatus ThreadPool::wait_for_impl(std::chrono::steady_clock::duration timeout) {
    auto core = impl_->core;
    if (detail::current_pool_identity() == core->identity.get()) {
        return WaitStatus::would_deadlock;
    }
    shutdown(ShutdownMode::drain);
    {
        std::unique_lock lock(core->mutex);
        if (!core->all_stopped.wait_for(lock, timeout, [&] { return core->live_workers == 0; })) {
            return WaitStatus::timeout;
        }
    }
    std::lock_guard thread_lock(core->thread_mutex);
    for (auto& worker : core->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    return WaitStatus::ready;
}

PoolState ThreadPool::state() const noexcept {
    auto core = impl_->core;
    std::lock_guard lock(core->mutex);
    return core->state;
}

MetricsSnapshot ThreadPool::metrics() const noexcept {
    auto core = impl_->core;
    return MetricsSnapshot{
        .workers = core->options.thread_count,
        .queue_capacity = core->options.queue_capacity,
        .queued_now = core->queued_now.load(std::memory_order_relaxed),
        .running_now = core->running_now.load(std::memory_order_relaxed),
        .accepted = core->accepted.load(std::memory_order_relaxed),
        .started = core->started.load(std::memory_order_relaxed),
        .completed = core->completed.load(std::memory_order_relaxed),
        .failed = core->failed.load(std::memory_order_relaxed),
        .cancelled = core->cancelled.load(std::memory_order_relaxed),
        .rejected_full = core->rejected_full.load(std::memory_order_relaxed),
        .rejected_shutdown = core->rejected_shutdown.load(std::memory_order_relaxed),
        .timed_out = core->timed_out.load(std::memory_order_relaxed),
    };
}

} // namespace projecta::concurrency
