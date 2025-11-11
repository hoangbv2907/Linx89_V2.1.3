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

    HWND Create(HWND hParent, int x, int y, int w, int h, const std::wstring& text, int id);
    static LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR refData);
    void Subclass();
    void Draw(LPDRAWITEMSTRUCT dis);
    void btnSetDraw(LPDRAWITEMSTRUCT dis);
    void HandleMouse(UINT msg);
    void SetText(const std::wstring& text);
    void SetBaseColor(COLORREF color);
    void Redraw();
    HWND GetHandle() const { return hButton; }

private:
    HWND hButton = nullptr;
    std::wstring caption;
    bool isHover = false;
    bool isPressed = false;
    COLORREF baseColor = RGB(220, 220, 220);
    COLORREF textColor = RGB(30, 30, 30);

    void DrawRoundedRect(HDC hdc, const RECT& rc, COLORREF fill, int radius);
};