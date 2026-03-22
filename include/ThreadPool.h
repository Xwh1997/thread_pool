#pragma once
#include <thread>
#include <vector>
#include <functional>
#include <future>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>

#include "ThreadSafeQueue.h"

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool& ) =delete;

    template<class F, class... Args>
    auto submit(F&& f, Args&&... args) ->std::future<std::invoke_result_t<F, Args...>>;
private:
    std::vector<std::thread> workers_;
    ThreadSafeQueue<std::function<void()>> task_queue_;

};

template <class F, class... Args>
auto ThreadPool::submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>>
{
    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    std::future<return_type> future = task->get_future();
    bool isSuccess = task_queue_.push([task]{
        (*task)();
    });
    if (!isSuccess) {
        throw std::runtime_error("thread pool is stop!");
    }
    return future;
}
