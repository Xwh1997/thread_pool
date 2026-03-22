#include <queue>
#include <stddef.h>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <utility>

template <typename T> 
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;

    ThreadSafeQueue(const ThreadSafeQueue& other) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue& other) = delete;
    bool push(T&& t);
    bool push(const T& t);
    std::optional<T> pop();
    std::optional<T> wait_and_pop();
    size_t size() const;
    bool empty() const;
    void stop();

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};

template <typename T> 
bool ThreadSafeQueue<T>::push(T&& t) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            return false;
        }
        queue_.push(std::move(t));
    }
    cv_.notify_one();
    return true;
}

template <typename T> 
bool ThreadSafeQueue<T>::push(const T& t) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            return false;
        }
        queue_.emplace(t);
    }
    cv_.notify_one();
    return true;
}

template <typename T> 
std::optional<T> ThreadSafeQueue<T>::pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return std::nullopt;
    }
    std::optional<T> t{std::move(queue_.front())};
    queue_.pop();
    return t;
}

template <typename T>
std::optional<T> ThreadSafeQueue<T>::wait_and_pop()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this](){ return !queue_.empty() || stop_; });

    if (stop_ && queue_.empty()) {
        return std::nullopt;
    }
    std::optional<T> t{std::move(queue_.front())};
    queue_.pop();
    return t;
}

template <typename T> 
size_t ThreadSafeQueue<T>::size() const { 
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size(); 
}

template <typename T> 
bool ThreadSafeQueue<T>::empty() const { 
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty(); 
}

template <typename T> 
void ThreadSafeQueue<T>::stop() { 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
}
