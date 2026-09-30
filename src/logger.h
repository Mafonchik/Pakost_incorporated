#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>

class AccessLogger {
private:
    std::ofstream log_file_;
    std::mutex mutex_;

    static std::string formatTime(std::chrono::system_clock::time_point tp) {
        auto in_time_t = std::chrono::system_clock::to_time_t(tp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
        std::tm tm_buf{};
        localtime_r(&in_time_t, &tm_buf);
        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    static std::string escape(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '"') out += "\\\"";
            else out += c;
        }
        return out;
    }

public:
    explicit AccessLogger(const std::string& filename = "access.log") {
        log_file_.open(filename, std::ios::out | std::ios::app);
    }

    void log(const std::string& query_body,
             uint32_t client_id,
             uint32_t worker_id,
             std::chrono::system_clock::time_point start_time,
             std::chrono::system_clock::time_point end_time,
             int status_code,
             const std::string& status_msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!log_file_.is_open()) return;

        log_file_ << "[" << formatTime(start_time) << " -> " << formatTime(end_time) << "] "
                  << "[ClientID: " << client_id << "] "
                  << "[WorkerID: " << worker_id << "] "
                  << "[Code: " << status_code << " (" << escape(status_msg) << ")] "
                  << "Query: \"" << escape(query_body) << "\"\n";
        log_file_.flush();
    }
};
