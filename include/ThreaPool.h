#include <thread>
#include <vector>
#include <functional>
#include <future>
#include <stddef.h>

#include <ThreadSafeQueue.h>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    ThreadPool(ThreadPool&&) = default;
    ThreadPool& operator=(ThreadPool&&) = default;

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
    return std::future<std::invoke_result_t<F, Args...>>();
}
