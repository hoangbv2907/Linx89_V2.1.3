#include "MessageLogger.h"

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
void MessageLogger::AddMessage(const std::wstring& text) {
	if (!hwnd_) return; // Kiểm tra nếu hwnd_ hợp lệ

	int len = GetWindowTextLengthW(hwnd_);  // Lấy độ dài hiện tại của văn bản
	SendMessage(hwnd_, EM_SETSEL, len, len);    // Đặt con trỏ vào cuối văn bản
	SendMessage(hwnd_, EM_REPLACESEL, FALSE, (LPARAM)(text + L"\r\n").c_str()); // Thêm văn bản mới với xuống dòng
	SendMessage(hwnd_, WM_VSCROLL, SB_BOTTOM, 0);   // Tự động cuộn xuống cuối
	UpdateWindow(hwnd_);    // Cập nhật hiển thị ngay lập tức
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