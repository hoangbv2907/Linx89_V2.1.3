#pragma once
#include <windows.h>
#include <string>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "UxTheme.lib")

class ModernButton {
public:
    ModernButton();
    ~ModernButton();

    //Tạo control REAL button thực tế
    HWND Create(HWND hParent, int x, int y, int w, int h, const std::wstring& text, int id);
    
	//Subclass procedure để xử lý vẽ và sự kiện chuột
    static LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR refData);

    //Các hàm thành viên
    void Subclass();
    
	//Hàm vẽ button
    void Draw(LPDRAWITEMSTRUCT dis);

	// Hàm xử lý vẽ button
    void btnSetDraw(LPDRAWITEMSTRUCT dis);

	// Hàm xử lý sự kiện chuột
    void HandleMouse(UINT msg);

	//Các hàm thiết lập thuộc tính
    void SetText(const std::wstring& text);

	// Thiết lập màu nền button
    void SetBaseColor(COLORREF color);

	//Cập nhật lại giao diện button
    void Redraw();

	//Lấy handle của button
    HWND GetHandle() const { return hButton; }

private:
	HWND hButton = nullptr; // Handle của button
	std::wstring caption;   // Văn bản hiển thị trên button
	bool isHover = false;   // Trạng thái hover chuột
	bool isPressed = false; // Trạng thái nhấn chuột
	COLORREF baseColor = RGB(220, 220, 220);    // Màu nền cơ bản của button
	COLORREF textColor = RGB(30, 30, 30);   // Màu chữ của button

	// Hàm vẽ hình chữ nhật bo góc
    void DrawRoundedRect(HDC hdc, const RECT& rc, COLORREF fill, int radius);
};