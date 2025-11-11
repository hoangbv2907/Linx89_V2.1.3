#include "ModernButton.h"
#include <windowsx.h>
#include <uxtheme.h>
#include <algorithm>

#ifndef GWL_USERDATA
#define GWL_USERDATA (-21)
#endif

ModernButton::ModernButton() {}

ModernButton::~ModernButton() {
    if (hButton) {
        RemoveWindowSubclass(hButton, ButtonSubclassProc, 1);
    }
}

HWND ModernButton::Create(HWND hParent, int x, int y, int w, int h, const std::wstring& text, int id) {
    caption = text;
    hButton = CreateWindowW(L"BUTTON", text.c_str(),
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        x, y, w, h, hParent, (HMENU)(INT_PTR)id, NULL, NULL);
    return hButton;
}

LRESULT CALLBACK ModernButton::ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR refData) {
    ModernButton* p = reinterpret_cast<ModernButton*>(refData);

    switch (msg) {
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        p->HandleMouse(msg);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, ButtonSubclassProc, id);
        break;
    }

    return DefSubclassProc(hwnd, msg, wp, lp);
}

void ModernButton::Subclass() {
    if (hButton) {
        SetWindowSubclass(hButton, ButtonSubclassProc, 1, (DWORD_PTR)this);
    }
}

void ModernButton::Draw(LPDRAWITEMSTRUCT dis) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    bool isDisabled = (GetWindowLongPtr(hButton, GWL_STYLE) & WS_DISABLED);
    bool isHighlighted = (GetWindowLongPtr(dis->hwndItem, GWL_USERDATA) == 1);

    COLORREF fillColor = baseColor;
    COLORREF txtColor = textColor;

    if (isDisabled) {
        fillColor = RGB(240, 240, 240);
        txtColor = RGB(150, 150, 150);
    }
    else if (isHighlighted) {
        fillColor = RGB(255, 230, 150);
        txtColor = RGB(0, 0, 0);
    }
    else if (isPressed) {
        fillColor = RGB(
            max(GetRValue(baseColor) - 30, 0),
            max(GetGValue(baseColor) - 30, 0),
            max(GetBValue(baseColor) - 30, 0)
        );
    }
    else if (isHover) {
        fillColor = RGB(
            min(GetRValue(baseColor) + 20, 255),
            min(GetGValue(baseColor) + 20, 255),
            min(GetBValue(baseColor) + 20, 255)
        );
    }

    DrawRoundedRect(hdc, rc, fillColor, 8);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, txtColor);
    DrawTextW(hdc, caption.c_str(), -1, (LPRECT)&rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void ModernButton::btnSetDraw(LPDRAWITEMSTRUCT dis) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    bool isEnabled = !(dis->itemState & ODS_DISABLED);
    bool isPressed = (dis->itemState & ODS_SELECTED);
    bool isHover = (dis->itemState & ODS_HOTLIGHT);

    COLORREF fillColor = RGB(220, 220, 220);
    COLORREF textColor = RGB(30, 30, 30);

    if (!isEnabled) {
        fillColor = RGB(240, 240, 240);
        textColor = RGB(150, 150, 150);
    }
    else if (isPressed) {
        fillColor = RGB(190, 190, 190);
    }
    else if (isHover) {
        fillColor = RGB(200, 200, 200);
    }

    HBRUSH brush = CreateSolidBrush(fillColor);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    DrawTextW(hdc, L"Set", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void ModernButton::HandleMouse(UINT msg) {
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!isHover) {
            isHover = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hButton, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hButton, NULL, TRUE);
        }
        break;

    case WM_MOUSELEAVE:
        isHover = false;
        isPressed = false;
        InvalidateRect(hButton, NULL, TRUE);
        break;

    case WM_LBUTTONDOWN:
        isPressed = true;
        InvalidateRect(hButton, NULL, TRUE);
        break;

    case WM_LBUTTONUP:
        isPressed = false;
        InvalidateRect(hButton, NULL, TRUE);
        break;
    }
}

void ModernButton::SetText(const std::wstring& text) {
    caption = text;
    if (hButton) InvalidateRect(hButton, NULL, TRUE);
}

void ModernButton::SetBaseColor(COLORREF color) {
    baseColor = color;
    if (hButton) InvalidateRect(hButton, NULL, TRUE);
}

void ModernButton::Redraw() {
    if (hButton) InvalidateRect(hButton, NULL, TRUE);
}

void ModernButton::DrawRoundedRect(HDC hdc, const RECT& rc, COLORREF fill, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
}