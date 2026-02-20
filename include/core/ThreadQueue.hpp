#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace core {

template <typename T> class ThreadQueue {
  public:
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(item));
        }
        cv_.notify_one();
    }

    template <typename Better>
    std::optional<T> waitPopBest(Better better) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]() { return stopped_ || !queue_.empty(); });
        if (stopped_ && queue_.empty()) {
            return std::nullopt;
        }
        auto bestIt = queue_.begin();
        for (auto it = std::next(queue_.begin()); it != queue_.end(); ++it) {
            if (better(*it, *bestIt)) {
                bestIt = it;
            }
        }
        T out = std::move(*bestIt);
        queue_.erase(bestIt);
        return out;
    }

    bool tryPop(T &out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
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
    std::deque<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;
};

} // namespace core
