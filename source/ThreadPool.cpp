#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t num_threads)
{
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                auto task_opt = task_queue_.wait_and_pop();
                if (!task_opt.has_value()) {
                    break;
                }
                
                (*task_opt)();
            }
        });
    }
}

ThreadPool::~ThreadPool()
{
    task_queue_.stop();
    for (std::thread& thread : workers_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}
