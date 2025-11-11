// ResourceTracker.h
#pragma once
#include <functional>
#include <vector>
#include <string>
#include <memory>
#include "Logger.h"

class ResourceTracker {
private:
    std::vector<std::pair<std::string, std::function<void()>>> cleanupTasks;
    std::string name_;

public:
    ResourceTracker(const std::string& name = "ResourceTracker") : name_(name) {}

    // Thêm cleanup task với tên để debug
    template<typename T>
    void addCleanup(const std::string& taskName, T&& task) {
        cleanupTasks.emplace_back(taskName, std::forward<T>(task));
        Logger::GetInstance().Write(L"[" + std::wstring(name_.begin(), name_.end()) +
            L"] Added cleanup task: " +
            std::wstring(taskName.begin(), taskName.end()));
    }

    // Cleanup tất cả resources
    void cleanupAll() {
        Logger::GetInstance().Write(L"[" + std::wstring(name_.begin(), name_.end()) +
            L"] Starting cleanup of " +
            std::to_wstring(cleanupTasks.size()) + L" resources");

        for (auto it = cleanupTasks.rbegin(); it != cleanupTasks.rend(); ++it) {
            const auto& taskName = it->first;
            const auto& task = it->second;

            try {
                Logger::GetInstance().Write(L"Cleaning up: " +
                    std::wstring(taskName.begin(), taskName.end()));
                task();
                Logger::GetInstance().Write(L"✓ Success: " +
                    std::wstring(taskName.begin(), taskName.end()));
            }
            catch (const std::exception& e) {
                std::string error = e.what();
                Logger::GetInstance().Write(L"✗ Failed cleanup [" +
                    std::wstring(taskName.begin(), taskName.end()) +
                    L"]: " +
                    std::wstring(error.begin(), error.end()), 2);
            }
            catch (...) {
                Logger::GetInstance().Write(L"✗ Unknown error in cleanup [" +
                    std::wstring(taskName.begin(), taskName.end()) +
                    L"]", 2);
            }
        }
        cleanupTasks.clear();
        Logger::GetInstance().Write(L"[" + std::wstring(name_.begin(), name_.end()) +
            L"] Cleanup completed");
    }

    // Manual cleanup của một task cụ thể
    bool cleanupTask(const std::string& taskName) {
        for (auto it = cleanupTasks.begin(); it != cleanupTasks.end(); ++it) {
            if (it->first == taskName) {
                try {
                    it->second();
                    cleanupTasks.erase(it);
                    Logger::GetInstance().Write(L"✓ Manually cleaned: " +
                        std::wstring(taskName.begin(), taskName.end()));
                    return true;
                }
                catch (...) {
                    Logger::GetInstance().Write(L"✗ Manual cleanup failed: " +
                        std::wstring(taskName.begin(), taskName.end()), 2);
                    return false;
                }
            }
        }
        return false;
    }

    ~ResourceTracker() {
        if (!cleanupTasks.empty()) {
            Logger::GetInstance().Write(L"[" + std::wstring(name_.begin(), name_.end()) +
                L"] Auto-cleaning in destructor");
            cleanupAll();
        }
    }

    // Kiểm tra số lượng tasks còn lại
    size_t getPendingCleanupCount() const { return cleanupTasks.size(); }

    // Xóa tất cả tasks mà không thực thi (khi transfer ownership)
    void clearWithoutCleanup() {
        cleanupTasks.clear();
    }
};