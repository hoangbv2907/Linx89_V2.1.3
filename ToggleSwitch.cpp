#include "ToggleSwitch.h"
#include "FontManager.h"
#include "ColorScheme.h"

ToggleSwitch::ToggleSwitch() {}

ToggleSwitch::~ToggleSwitch() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
    }
}

HWND ToggleSwitch::Create(HWND hParent, int x, int y, int width, int height, int id) {
    hwnd_ = CreateWindowW(L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        x, y, width, height,
        hParent, (HMENU)(INT_PTR)id, NULL, NULL);

    if (hwnd_ && FontManager::GetInstance().GetToggleFont()) {
        SendMessage(hwnd_, WM_SETFONT,
            (WPARAM)FontManager::GetInstance().GetToggleFont(), TRUE); // ✅ ĐÃ SỬA: GetToggleFont() đã trả về HFONT
    }

    return hwnd_;
}

void ToggleSwitch::Draw(LPDRAWITEMSTRUCT dis) {
    if (!dis) return;

    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    auto& colors = ColorScheme::GetInstance();
    HBRUSH bg = CreateSolidBrush(isOn_ ? colors.GetToggleOnColor() : colors.GetToggleOffColor());
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    int diameter = rc.bottom - rc.top - 6;
    int margin = 3;
    int xPos = isOn_ ? (rc.right - diameter - margin) : (rc.left + margin);

    HBRUSH circle = CreateSolidBrush(colors.GetToggleKnobColor());
    HGDIOBJ oldBrush = SelectObject(hdc, circle);
    Ellipse(hdc, xPos, rc.top + margin, xPos + diameter, rc.bottom - margin);
    SelectObject(hdc, oldBrush);
    DeleteObject(circle);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    HFONT toggleFont = FontManager::GetInstance().GetToggleFont();
    HFONT oldFont = (HFONT)SelectObject(hdc, toggleFont);
    const wchar_t* text = isOn_ ? L"ON" : L"OFF";
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

void ToggleSwitch::Toggle() {
    isOn_ = !isOn_;
    if (hwnd_) InvalidateRect(hwnd_, NULL, TRUE);
}

void ToggleSwitch::SetState(bool state) {
    if (isOn_ != state) {
        isOn_ = state;
        if (hwnd_) InvalidateRect(hwnd_, NULL, TRUE);
    }
}