#pragma once
#include <string>
#include <vector>

enum class PrinterStateType {
    Disconnected,
    Connecting,
    Reconnecting,
    StartingJet,
    StopingJet,
    Connected,
    Idle,
    Ready,
    Printing,
    Error,
    Unknown
};

//
// Request Types
//
enum class RequestType {
    RequestStatus,
    RequestPrintCount,
    RequestSetCount,
    RequestStartPrint,
    RequestStopPrint,
    RequestStartJet,
    RequestStopJet,
    RequestConnect,
    RequestDisconnect
};

//
// Printer State Structure (lưu trong PrinterModel)
//
struct PrinterState {
    PrinterStateType status = PrinterStateType::Disconnected;

    bool jetOn = false;
    bool printing = false;

    int printedCount = 0;
    int targetCount = 0;

    std::wstring jobId;
    std::wstring errorMessage;
    std::wstring statusText;

    // Helper methods
    bool IsConnected() const {
        return status == PrinterStateType::Connected ||
            status == PrinterStateType::Idle ||
            status == PrinterStateType::Printing;
    }

    bool CanPrint() const {
        return IsConnected() && status != PrinterStateType::Error;
    }
};

//
// Request Structure
//
struct Request {
    RequestType type;
    std::vector<uint8_t> payload;
    std::wstring data;      // message text
    int count = 0;
    std::wstring ipAddress; // not used but kept for compatibility
};
