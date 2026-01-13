
//CommonTypes.h
#pragma once
#include <string>
#include <vector>

enum class PrinterStateType {
    Disconnected,   //chua kết nối  
    Connected,      //da ket noi
    Connecting,     //dang ket noi
    Reconnecting,   //dang ket noi lai
    StartingJet,    //dang bat jet
    StopingJet,     //dang tat jet
    Idle,           //jet off, máy vừa bật lên
    Ready,           // paused jet on, print off
    Printing,       //jet on, print on
    Error,          // có lỗi
    Unknown            // trạng thái không xác định
};

enum class RequestType {
    RequestStatus,
    RequestPrintCount,
    RequestSetCount,
    RequestStartPrint,
    RequestStopPrint,
    RequestStartJet,
    RequestStopJet,
    RequestConnect,
    RequestDisconnect,
    RequestUploadContent,
    RequestUploadRemoteField
};

struct PrinterState {
    PrinterStateType status = PrinterStateType::Disconnected;
    bool jetOn = false;
    bool printing = false;
    bool jetTransitioning = false;
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

struct Request {
    RequestType type;
    std::vector<uint8_t> payload;
    std::wstring data;      // message text
    int count = 0;
    std::wstring ipAddress; // not used but kept for compatibility
    std::wstring fieldName; // for remote field updates
};
// Convert PrinterState to human-readable text
inline std::wstring StateToText(const PrinterState& st) {
    switch (st.status) {
    case PrinterStateType::StartingJet:
        return L"🟡 STARTING JET...";
    case PrinterStateType::StopingJet:
        return L"🟠 STOPPING JET...";
    case PrinterStateType::Printing:
        return L"🔵 PRINTING";
    case PrinterStateType::Ready:
        return L"🟢 READY";
    case PrinterStateType::Idle:
        return L"⚪ IDLE (Jet OFF)";
    case PrinterStateType::Error:
        return L"🔴 ERROR: " + st.errorMessage;
    case PrinterStateType::Connecting:
        return L"🔄 CONNECTING...";
    case PrinterStateType::Reconnecting:
        return L"🔁 RECONNECTING...";
    case PrinterStateType::Disconnected:
        return L"❌ DISCONNECTED";
    default:
        return L"❓ UNKNOWN";
    }
}