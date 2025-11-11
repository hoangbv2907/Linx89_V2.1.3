#pragma once
#include <string>
#include "CommonTypes.h"  // GIỮ LẠI
#include <mutex>
// #include "CommonDefs.h"  // XÓA DÒNG NÀY - KHÔNG CẦN THIẾT

class PrinterModel {
public:
    PrinterModel() {
        // Khởi tạo currentState_ với giá trị mặc định
        currentState_.status = PrinterStatus::Disconnected;
        currentState_.statusText = L"Chưa kết nối";
        currentState_.jetOn = false;
        currentState_.printing = false;
        currentState_.printedCount = 0;
        currentState_.targetCount = 0;
    }

    void SetState(PrinterState state) {
        std::lock_guard<std::mutex> lock(mutex_);
        currentState_ = state;
    }

    PrinterState GetState() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return currentState_;
    }

    void SetStatusText(const std::wstring& status) {
        std::lock_guard<std::mutex> lock(mutex_);
        statusText_ = status;
        currentState_.statusText = status; // Đồng bộ với currentState_
    }

    std::wstring GetStatusText() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return statusText_;
    }

    void SetCurrentJob(const std::wstring& jobId, int totalCount) {
        std::lock_guard<std::mutex> lock(mutex_);
        currentJobId_ = jobId;
        jobTotal_ = totalCount;
        jobCurrent_ = 0;
        currentState_.jobId = jobId;
        currentState_.targetCount = totalCount;
        currentState_.printedCount = 0;
    }

    void UpdateJobProgress(int currentCount) {
        std::lock_guard<std::mutex> lock(mutex_);
        jobCurrent_ = currentCount;
        currentState_.printedCount = currentCount;
    }

    void ClearCurrentJob() {
        std::lock_guard<std::mutex> lock(mutex_);
        currentJobId_.clear();
        jobTotal_ = 0;
        jobCurrent_ = 0;
        currentState_.jobId.clear();
        currentState_.targetCount = 0;
        currentState_.printedCount = 0;
    }

    void SetLastError(const std::wstring& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = error;
        currentState_.errorMessage = error;
        currentState_.status = PrinterStatus::Error;
    }

    std::wstring GetLastError() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastError_;
    }

    void SetConnectionInfo(const std::wstring& ip, int port) {
        std::lock_guard<std::mutex> lock(mutex_);
        ipAddress_ = ip;
        port_ = port;
    }

    std::wstring GetIpAddress() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ipAddress_;
    }

    int GetPort() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return port_;
    }

    bool HasPendingPrintJob() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return hasPendingJob_;
    }

    void ClearPendingJob() {
        std::lock_guard<std::mutex> lock(mutex_);
        hasPendingJob_ = false;
        printContent_.clear();
    }

    int GetCurrentCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return jobCurrent_;
    }

    int GetTargetCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return jobTotal_;
    }

    // Thêm method để cập nhật trạng thái in
    void SetPrintingState(bool isPrinting) {
        std::lock_guard<std::mutex> lock(mutex_);
        currentState_.printing = isPrinting;
        currentState_.status = isPrinting ? PrinterStatus::Printing : PrinterStatus::Idle;
    }

    // Thêm method để cập nhật trạng thái jet
    void SetJetState(bool jetOn) {
        std::lock_guard<std::mutex> lock(mutex_);
        currentState_.jetOn = jetOn;
    }

private:
    mutable std::mutex mutex_;
    PrinterState currentState_;  // XÓA KHỞI TẠO TRỰC TIẾP
    std::wstring statusText_ = L"Chưa kết nối";
    std::wstring lastError_;
    std::wstring printContent_;
    bool hasPendingJob_ = false;

    std::wstring currentJobId_;
    int jobTotal_ = 0;
    int jobCurrent_ = 0;

    std::wstring ipAddress_;
    int port_ = 9100;
};