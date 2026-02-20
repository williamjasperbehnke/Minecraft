#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace core {

template <typename T> class ThreadQueue {
  public:
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(item));
        }
        cv_.notify_one();
    }

    std::optional<T> waitPop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]() { return stopped_ || !queue_.empty(); });
        if (stopped_ && queue_.empty()) {
            return std::nullopt;
        }
        T out = std::move(queue_.front());
        queue_.pop();
        return out;
    }

    bool tryPop(T &out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

  private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;
};

} // namespace core
