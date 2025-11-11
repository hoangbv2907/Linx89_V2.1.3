#include "MessageLogger.h"

MessageLogger::MessageLogger() {}

MessageLogger::~MessageLogger() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
    }
}

HWND MessageLogger::Create(HWND hParent, int x, int y, int width, int height) {
    LoadLibraryW(L"Msftedit.dll");

    hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        x, y, width, height, hParent, NULL, NULL, NULL);
    return hwnd_;
}

void MessageLogger::AddMessage(const std::wstring& text) {
    if (!hwnd_) return;

    int len = GetWindowTextLengthW(hwnd_);
    SendMessage(hwnd_, EM_SETSEL, len, len);
    SendMessage(hwnd_, EM_REPLACESEL, FALSE, (LPARAM)(text + L"\r\n").c_str());
    SendMessage(hwnd_, WM_VSCROLL, SB_BOTTOM, 0);
    UpdateWindow(hwnd_);
}

void MessageLogger::Clear() {
    if (hwnd_) {
        SetWindowTextW(hwnd_, L"");
    }
}

void MessageLogger::SetFont(HFONT hFont) {
    if (hwnd_ && hFont) {
        SendMessage(hwnd_, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
}