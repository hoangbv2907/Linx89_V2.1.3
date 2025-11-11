#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "CommonTypes.h"
#include "CommonDefs.h"

struct LogMessage {
    std::wstring text;
    int level = 0; // 0=INFO, 1=WARNING, 2=ERROR
};

struct PrinterStateMessage {
    PrinterState state;
    std::wstring statusText;
    std::wstring additionalInfo;
};

struct ConnectionMessage {
    bool connected;
    std::wstring ipAddress;
    int port;
};

struct PrintProgressMessage {
    int current;
    int total;
    std::wstring jobId;
};

struct ButtonStateMessage {
    PrinterState state;
};