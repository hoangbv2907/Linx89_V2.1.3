#pragma once
#include "CommonTypes.h" 
#include <mutex>

class StateManager {
public:
    static StateManager& GetInstance() {
        static StateManager instance;
        return instance;
    }

    void SetPrinterState(PrinterState state) {
        std::lock_guard<std::mutex> lock(mutex_);
        currentState_ = state;
    }

    PrinterState GetPrinterState() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return currentState_;
    }

    void SetStatusText(const std::wstring& status) {
        std::lock_guard<std::mutex> lock(mutex_);
        currentState_.statusText = status;
        
    }

    std::wstring GetStatusText() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return currentState_.statusText;
    }

private:
    StateManager() {
        // Khởi tạo currentState_ với giá trị mặc định
        currentState_.status = PrinterStatus::Disconnected;
        currentState_.statusText = L"Disconnected";
        currentState_.jetOn = false;
        currentState_.printing = false;
        currentState_.printedCount = 0;
        currentState_.targetCount = 0;
        currentState_.jobId = L"";
        currentState_.errorMessage = L"";
    }

    ~StateManager() = default;

    mutable std::mutex mutex_;
    PrinterState currentState_;  
};