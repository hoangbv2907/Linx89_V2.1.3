#pragma once
#include <windows.h>
#include <string>
#include <richedit.h>

//Quản lý một RichEdit control
//Hiển thị log từ AppController sang UI
//Có thể áp dụng font tùy chỉnh

class MessageLogger {
public:
    MessageLogger();
    ~MessageLogger();

	// Tạo RichEdit control
    HWND Create(HWND hParent, int x, int y, int width, int height);
	// Thêm tin nhắn vào RichEdit control
    void AddMessage(const std::wstring& text);
	// Xóa tất cả tin nhắn trong RichEdit control
    void Clear();
	// Đặt font tùy chỉnh cho RichEdit control
    void SetFont(HFONT hFont);

private:
	HWND hwnd_ = nullptr;   // Handle của RichEdit control
};