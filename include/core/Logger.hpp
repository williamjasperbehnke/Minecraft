#pragma once

#include <mutex>
#include <string>

namespace core {

class Logger {
  public:
    static Logger &instance();

    void info(const std::string &msg);
    void warn(const std::string &msg);
    void error(const std::string &msg);

  private:
    Logger() = default;
    std::mutex mutex_;
    void log(const char *level, const std::string &msg);
};

} // namespace core
