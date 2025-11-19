//================================
//Lớp xử lý logic máy in chính
//================================
#pragma once
#include <string>
#include "CommonTypes.h"

struct PrintJob {
    std::wstring content;
    std::wstring ipAddress;
    int count;
};

class PrinterCore {
public:
    static PrinterCore& GetInstance() {
        static PrinterCore instance;
        return instance;
    }

    bool Connect(const std::wstring& ip);
    bool Disconnect();
    bool StartPrinting(const PrintJob& job);
    bool StopPrinting();
    PrinterState GetState() const { return currentState_; }

private:
    PrinterCore() = default;
    PrinterState currentState_;
};
