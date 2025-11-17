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

//Tạo control REAL button thực tế
HWND ModernButton::Create(HWND hParent, int x, int y, int w, int h, const std::wstring& text, int id) {
    caption = text;
    hButton = CreateWindowW(L"BUTTON", text.c_str(),
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        x, y, w, h, hParent, (HMENU)(INT_PTR)id, NULL, NULL);
    return hButton;
}

//Subclass procedure để xử lý vẽ và sự kiện chuột
LRESULT CALLBACK ModernButton::ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR refData) {
	ModernButton* p = reinterpret_cast<ModernButton*>(refData); // Lấy con trỏ đến đối tượng ModernButton

	// Xử lý các thông điệp
    switch (msg) {
	case WM_MOUSEMOVE:  // Chuột di chuyển vào button
	case WM_MOUSELEAVE: // Chuột rời khỏi button
	case WM_LBUTTONDOWN:   // Chuột nhấn xuống
	case WM_LBUTTONUP:  // Chuột thả ra
		p->HandleMouse(msg);    // Gọi hàm xử lý sự kiện chuột
        break;
	case WM_NCDESTROY:  // Button bị hủy
		RemoveWindowSubclass(hwnd, ButtonSubclassProc, id); // Gỡ subclass
        break;
    }
	//  Trả về xử lý mặc định
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// Hàm vẽ hình chữ nhật bo góc
void ModernButton::Subclass() {
    if (hButton) {
        SetWindowSubclass(hButton, ButtonSubclassProc, 1, (DWORD_PTR)this);
    }
}

//Hàm vẽ button
void ModernButton::Draw(LPDRAWITEMSTRUCT dis) {
	HDC hdc = dis->hDC; // Lấy device context để vẽ
	RECT rc = dis->rcItem;  // Lấy vùng vẽ

	// Kiểm tra trạng thái button
    bool isDisabled = (GetWindowLongPtr(hButton, GWL_STYLE) & WS_DISABLED);
    bool isHighlighted = (GetWindowLongPtr(dis->hwndItem, GWL_USERDATA) == 1);

	// Xác định màu sắc dựa trên trạng thái
    COLORREF fillColor = baseColor;
    COLORREF txtColor = textColor;

	// Thay đổi màu sắc dựa trên trạng thái
	if (isDisabled) {   // Nếu button bị vô hiệu hóa
        fillColor = RGB(240, 240, 240);
        txtColor = RGB(150, 150, 150);
    }
	else if (isHighlighted) {   // Nếu button được đánh dấu
        fillColor = RGB(255, 230, 150);
        txtColor = RGB(0, 0, 0);
    }
	else if (isPressed) {   // Nếu button đang được nhấn
        fillColor = RGB(
            max(GetRValue(baseColor) - 30, 0),
            max(GetGValue(baseColor) - 30, 0),
            max(GetBValue(baseColor) - 30, 0)
        );
    }
	else if (isHover) { // Nếu chuột hover lên button
        fillColor = RGB(
            min(GetRValue(baseColor) + 20, 255),
            min(GetGValue(baseColor) + 20, 255),
            min(GetBValue(baseColor) + 20, 255)
        );
    }
	// Vẽ nền button
    DrawRoundedRect(hdc, rc, fillColor, 8);

	// Vẽ viền button
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);

	// Vẽ văn bản trên button
    SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, txtColor);
    DrawTextW(hdc, caption.c_str(), -1, (LPRECT)&rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// Hàm xử lý vẽ button
void ModernButton::btnSetDraw(LPDRAWITEMSTRUCT dis) {
   
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
	// Trạng thái button
    bool isEnabled = !(dis->itemState & ODS_DISABLED);
    bool isPressed = (dis->itemState & ODS_SELECTED);
    bool isHover = (dis->itemState & ODS_HOTLIGHT);

	// Xác định màu sắc dựa trên trạng thái
    COLORREF fillColor = RGB(220, 220, 220);
    COLORREF textColor = RGB(30, 30, 30);

	// Thay đổi màu sắc dựa trên trạng thái
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
	// Vẽ nền button
    HBRUSH brush = CreateSolidBrush(fillColor);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
	// Vẽ viền button
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
	// Vẽ văn bản trên button
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    DrawTextW(hdc, L"Set", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// Hàm xử lý sự kiện chuột
void ModernButton::HandleMouse(UINT msg) {
    switch (msg) {
	case WM_MOUSEMOVE:  // Chuột di chuyển vào button
        if (!isHover) {
            isHover = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hButton, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hButton, NULL, TRUE);
        }
        break;

	case WM_MOUSELEAVE: // Chuột rời khỏi button
        isHover = false;
        isPressed = false;
        InvalidateRect(hButton, NULL, TRUE);
        break;

	case WM_LBUTTONDOWN:    // Chuột nhấn xuống
        isPressed = true;
        InvalidateRect(hButton, NULL, TRUE);
        break;

	case WM_LBUTTONUP:  // Chuột thả ra
        isPressed = false;
        InvalidateRect(hButton, NULL, TRUE);
        break;
    }
}

//Các hàm thiết lập thuộc tính
void ModernButton::SetText(const std::wstring& text) {
	caption = text; // Cập nhật văn bản
	if (hButton) InvalidateRect(hButton, NULL, TRUE);   // Yêu cầu vẽ lại button
}

// Thiết lập màu nền button
void ModernButton::SetBaseColor(COLORREF color) {
	baseColor = color;  // Cập nhật màu nền
	if (hButton) InvalidateRect(hButton, NULL, TRUE);   // Yêu cầu vẽ lại button
}

//Cập nhật lại giao diện button
void ModernButton::Redraw() {
	if (hButton) InvalidateRect(hButton, NULL, TRUE);   // Yêu cầu vẽ lại button
}

// Hàm vẽ hình chữ nhật bo góc
void ModernButton::DrawRoundedRect(HDC hdc, const RECT& rc, COLORREF fill, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
}