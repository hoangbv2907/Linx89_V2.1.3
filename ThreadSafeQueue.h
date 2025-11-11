#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <algorithm>
template<typename T>
class ThreadSafeQueue {
public:
    void Push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(value);
        condition_.notify_one();
    }

    bool Pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        value = queue_.front();
        queue_.pop();
        return true;
    }

    bool WaitPop(T& value, int timeoutMs = 100) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            condition_.wait_for(lock, std::chrono::milliseconds(timeoutMs));
            if (queue_.empty()) {
                return false;
            }
        }
        value = queue_.front();
        queue_.pop();
        return true;
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    std::condition_variable condition_;
};