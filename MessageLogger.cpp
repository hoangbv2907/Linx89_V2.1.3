#include "MessageLogger.h"
#include "MessageDef.h"

MessageLogger::MessageLogger() {}

MessageLogger::~MessageLogger() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
    }
}
// Tạo RichEdit control
HWND MessageLogger::Create(HWND hParent, int x, int y, int width, int height) {
	LoadLibraryW(L"Msftedit.dll");  // Đảm bảo thư viện RichEdit được tải
    hwnd_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,       // style viền 3D
        L"RICHEDIT50W",         // RichEdit version 5.0 (hiện đại)
        L"",                    // initial text rỗng
        WS_CHILD | WS_VISIBLE   // window child, visible
        | ES_MULTILINE          // nhiều dòng
        | ES_AUTOVSCROLL        // tự scroll khi xuống dòng
        | ES_READONLY           // chỉ đọc, người dùng không gõ
        | WS_VSCROLL,           // thanh scroll dọc
        x, y, width, height,
        hParent, NULL, NULL, NULL
    );
    return hwnd_;
}
// Thêm tin nhắn vào RichEdit control

void MessageLogger::AddMessage(const std::wstring& text, int level) {
    if (!hwnd_) return;

    int len = GetWindowTextLengthW(hwnd_);
    SendMessage(hwnd_, EM_SETSEL, len, len);

    CHARFORMAT2 cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;

    switch (level) {
    case 1: // WARNING
        cf.crTextColor = RGB(255, 140, 0);
        break;
    case 2: // ERROR
        cf.crTextColor = RGB(220, 20, 60);
        break;
    case 3: // DEBUG
        cf.crTextColor = RGB(0, 120, 215);
        break;
    case 4:
        cf.crTextColor = RGB(0, 255, 0);
        break;
    default: // INFO
        cf.crTextColor = RGB(30, 30, 30);
        break;
    }

    SendMessage(hwnd_, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessage(hwnd_, EM_REPLACESEL, FALSE, (LPARAM)(text + L"\r\n").c_str());
    SendMessage(hwnd_, WM_VSCROLL, SB_BOTTOM, 0);
    UpdateWindow(hwnd_);
}
// Xóa tất cả tin nhắn trong RichEdit control
void MessageLogger::Clear() {
    if (hwnd_) {
		SetWindowTextW(hwnd_, L""); // Xóa văn bản
    }
}
// Đặt font tùy chỉnh cho RichEdit control
void MessageLogger::SetFont(HFONT hFont) {
	if (hwnd_ && hFont) {   // Kiểm tra nếu hwnd_ và hFont hợp lệ
		SendMessage(hwnd_, WM_SETFONT, (WPARAM)hFont, TRUE);    // Áp dụng font mới và vẽ lại
    }
}