#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace core {

class TickCounter {
  public:
    TickCounter(float tickIntervalSeconds, int maxTicksPerUpdate)
        : tickIntervalSeconds_(tickIntervalSeconds), maxTicksPerUpdate_(maxTicksPerUpdate) {}

    int consume(float dtSeconds) {
        if (dtSeconds <= 0.0f || tickIntervalSeconds_ <= 0.0f) {
            return 0;
        }
        accumulatorSeconds_ += dtSeconds;
        const int availableTicks = static_cast<int>(accumulatorSeconds_ / tickIntervalSeconds_);
        const int maxTicks = (maxTicksPerUpdate_ > 0) ? maxTicksPerUpdate_
                                                      : std::numeric_limits<int>::max();
        const int ticks = std::min(maxTicks, availableTicks);
        if (ticks <= 0) {
            return 0;
        }
        accumulatorSeconds_ -= static_cast<float>(ticks) * tickIntervalSeconds_;
        tickCount_ += static_cast<std::uint64_t>(ticks);
        return ticks;
    }

    std::uint64_t tickCount() const {
        return tickCount_;
    }

    void reset() {
        accumulatorSeconds_ = 0.0f;
        tickCount_ = 0;
    }

  private:
    float tickIntervalSeconds_ = 0.0f;
    int maxTicksPerUpdate_ = 0;
    float accumulatorSeconds_ = 0.0f;
    std::uint64_t tickCount_ = 0;
};

} // namespace core
