#pragma once
#include "CommonTypes.h"
#include <queue>
#include <mutex>
#include <condition_variable>

class RequestQueue {
public:
    void Push(const Request& request) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(request);
        condition_.notify_one();
    }

    bool Pop(Request& request, int timeoutMs = 100) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (queue_.empty()) {
            condition_.wait_for(lock, std::chrono::milliseconds(timeoutMs));
            if (queue_.empty()) {
                return false;
            }
        }

        request = queue_.front();
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
    std::queue<Request> queue_;
    std::condition_variable condition_;
};