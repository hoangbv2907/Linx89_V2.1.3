#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <memory>

class Logger {
public:
    static Logger& GetInstance() {
        static Logger instance;
        return instance;
    }

    void Write(const std::wstring& message, int level = 0) {
        std::lock_guard<std::mutex> lock(logMutex_);

        std::wstring levelStr;
        switch (level) {
        case 1: levelStr = L"[WARNING] "; break;
        case 2: levelStr = L"[ERROR] "; break;
        case 3: levelStr = L"[DEBUG] "; break;
        default: levelStr = L"[INFO] "; break;
        }

        std::wstring logEntry = GetCurrentTime() + L" " + levelStr + message;

        // Output to debugger
        OutputDebugStringW((logEntry + L"\n").c_str());

        // Write to file if enabled
        if (logFile_.is_open()) {
            logFile_ << logEntry << std::endl;
        }
    }

    void SetLogFile(const std::wstring& filename) {
        std::lock_guard<std::mutex> lock(logMutex_);
        if (logFile_.is_open()) {
            logFile_.close();
        }
        logFile_.open(filename, std::ios::app);
    }

    void EnableConsoleOutput(bool enable) {
        consoleOutput_ = enable;
    }

private:
    Logger() {
        // Default log file
        SetLogFile(L"linx_controller.log");
        Write(L"Logger initialized");
    }

    ~Logger() {
        if (logFile_.is_open()) {
            logFile_.close();
        }
    }

    std::wstring GetCurrentTime() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buffer[64];
        swprintf_s(buffer, L"%02d:%02d:%02d.%03d",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return std::wstring(buffer);
    }

    std::wstring logFilename_;
    std::wofstream logFile_;
    mutable std::mutex logMutex_;
    bool consoleOutput_ = false;
};