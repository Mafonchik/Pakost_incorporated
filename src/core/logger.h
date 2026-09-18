#pragma once
#include <fstream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

class AccessLogger {
private:
    std::ofstream log_file_;

    std::string formatTime(std::chrono::system_clock::time_point tp) {
        auto in_time_t = std::chrono::system_clock::to_time_t(tp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

public:
    AccessLogger(const std::string& filename = "access.log") {
        log_file_.open(filename, std::ios::out | std::ios::app);
    }

    void log(const std::string& query_body, 
             uint32_t client_id, 
             uint32_t worker_id, 
             std::chrono::system_clock::time_point start_time, 
             std::chrono::system_clock::time_point end_time, 
             int status_code, 
             const std::string& status_msg) {
        if (!log_file_.is_open()) return;

        log_file_ << "[" << formatTime(start_time) << " -> " << formatTime(end_time) << "] "
                  << "[ClientID: " << client_id << "] "
                  << "[WorkerID: " << worker_id << "] "
                  << "[Code: " << status_code << " (" << status_msg << ")] "
                  << "Query: \"" << query_body << "\"\n";
        log_file_.flush();
    }
};