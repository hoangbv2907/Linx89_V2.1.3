#pragma once
#include <string>
#include <vector>

// Printer Status Enum
enum class PrinterStatus {
    Disconnected,
    Connecting,
    Connected,
    Idle,
    Printing,
    Error,
    Unknown
};

// Request Types
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

// Printer State Structure
struct PrinterState {
    PrinterStatus status = PrinterStatus::Disconnected;
    bool jetOn = false;
    bool printing = false;
    int printedCount = 0;
    int targetCount = 0;
    std::wstring jobId;
    std::wstring errorMessage;
    std::wstring statusText;

    // Helper methods
    bool IsConnected() const {
        return status == PrinterStatus::Connected ||
            status == PrinterStatus::Idle ||
            status == PrinterStatus::Printing;
    }

    bool CanPrint() const {
        return IsConnected() && status != PrinterStatus::Error;
    }
};

// Request Structure
struct Request {
    RequestType type;
    std::vector<uint8_t> payload;
    std::wstring data; // For text data like print content
    int count = 0;
    std::wstring ipAddress;
};