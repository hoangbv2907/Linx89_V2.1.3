#include "PrinterCore.h"
#include "StateManager.h"

bool PrinterCore::Connect(const std::wstring& ip) {
    // Giả lập kết nối - thay bằng logic thực tế
    currentState_.status = PrinterStatus::Connected;
    currentState_.statusText = L"Đã kết nối";
    currentState_.jetOn = false;
    currentState_.printing = false;
    StateManager::GetInstance().SetPrinterState(currentState_);
    return true;
}

bool PrinterCore::Disconnect() {
    // Giả lập ngắt kết nối
    currentState_.status = PrinterStatus::Disconnected;
    currentState_.statusText = L"Ngắt kết nối";
    currentState_.jetOn = false;
    currentState_.printing = false;
    StateManager::GetInstance().SetPrinterState(currentState_);
    return true;
}

bool PrinterCore::StartPrinting(const PrintJob& job) {
    // Giả lập bắt đầu in
    currentState_.status = PrinterStatus::Printing;
    currentState_.statusText = L"Đang in";
    currentState_.printing = true;
    currentState_.targetCount = job.count;
    currentState_.printedCount = 0;
    currentState_.jobId = L"job_" + std::to_wstring(rand());
    StateManager::GetInstance().SetPrinterState(currentState_);
    return true;
}

bool PrinterCore::StopPrinting() {
    // Giả lập dừng in
    currentState_.status = PrinterStatus::Idle;
    currentState_.statusText = L"Sẵn sàng";
    currentState_.printing = false;
    StateManager::GetInstance().SetPrinterState(currentState_);
    return true;
}