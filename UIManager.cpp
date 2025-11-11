#include "UIManager.h"
#include "FontManager.h"
#include "Logger.h"
#include <commctrl.h>
#include <richedit.h>
#include <algorithm>
#include "CommonTypes.h"

UIManager::UIManager() {}

UIManager::~UIManager() {}

bool UIManager::Initialize(HWND hParent) {
    hParent_ = hParent;

    // Load RichEdit library
    LoadLibraryW(L"Msftedit.dll");

    // Create custom controls
    toggleSwitch_ = std::make_unique<ToggleSwitch>();
    messageLogger_ = std::make_unique<MessageLogger>();

    Logger::GetInstance().Write(L"UIManager initialized");
    return true;
}

void UIManager::CreateControls() {
    if (!hParent_) return;

    // IP Address + toggle
    CreateStatic(20, 20, 100, 20, L"Địa chỉ IP:");
    hIpAddress_ = CreateWindowW(WC_IPADDRESSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        150, 20, 150, 23, hParent_, NULL, NULL, NULL);

    toggleSwitch_->Create(hParent_, 320, 20, 60, 26, IDC_TOGGLE);

    // Nội dung in
    CreateStatic(20, 60, 100, 20, L"Nội dung in:");
    hInputValue_ = CreateEdit(150, 60, 240, 60, L"");
    hBtnUpload_ = CreateButton(400, 60, 80, 60, L"Tải Lên", IDC_BTN_UPLOAD);

    // Số lượng
    CreateStatic(20, 140, 100, 20, L"Số lượng:");
    hCount_ = CreateEdit(150, 140, 80, 23, L"1");

    // Hiển thị số lượng đã đạt được
    CreateStatic(240, 140, 120, 23, L"Đã đạt được:");
    hCurrentCount_ = CreateWindowW(L"STATIC", L"0",
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        350, 140, 60, 22, hParent_,
        (HMENU)IDC_CURRENT_COUNT, NULL, NULL);

    // Modern buttons
    btnStart_.Create(hParent_, 20, 180, 140, 40, L"KHỞI ĐỘNG", IDC_BTN_START);
    btnPrint_.Create(hParent_, 180, 180, 140, 40, L"IN", IDC_BTN_PRINT);
    btnStop_.Create(hParent_, 340, 180, 140, 40, L"DỪNG", IDC_BTN_STOP);
    btnClear_.Create(hParent_, 380, 305, 100, 20, L"XÓA NHẬT KÝ", IDC_BTN_CLEAR);
    btnSet_.Create(hParent_, 420, 140, 60, 23, L"SET", IDC_BTN_SET);

    // Subclass các nút
    btnStart_.Subclass();
    btnPrint_.Subclass();
    btnStop_.Subclass();
    btnClear_.Subclass();
    btnSet_.Subclass();

    // Dòng trạng thái
    hPrinterState_ = CreateStatic(20, 240, 460, 24, L"TRẠNG THÁI: CHƯA KẾT NỐI");
    SetWindowLongPtr(hPrinterState_, GWL_STYLE,
        GetWindowLongPtr(hPrinterState_, GWL_STYLE) | SS_CENTER);

    // Status display
    hStatusDisplay_ = CreateStatic(20, 270, 460, 24, L"CHƯA KẾT NỐI");
    SetWindowLongPtr(hStatusDisplay_, GWL_STYLE,
        GetWindowLongPtr(hStatusDisplay_, GWL_STYLE) | SS_CENTER);

    // Log box
    CreateStatic(20, 305, 100, 20, L"NHẬT KÝ:");
    messageLogger_->Create(hParent_, 20, 330, 460, 120);

    // Apply fonts
    ApplyFontToAllControls();

    // Set default IP
    SendMessage(hIpAddress_, IPM_SETADDRESS, 0, (LPARAM)MAKEIPADDRESS(127, 0, 0, 1));

    // Initial state - SỬA DÒNG NÀY
    PrinterState initialState;
    initialState.status = PrinterStatus::Disconnected;
    initialState.statusText = L"Chưa kết nối";
    UpdateButtonStates(initialState);

    Logger::GetInstance().Write(L"UI controls created");
}

void UIManager::AddMessage(const std::wstring& text) {
    if (messageLogger_) {
        messageLogger_->AddMessage(text);
    }
}

void UIManager::ClearMessages() {
    if (messageLogger_) {
        messageLogger_->Clear();
    }
}

void UIManager::UpdatePrinterStatus(const std::wstring& text) {
    if (hStatusDisplay_) {
        SetWindowTextW(hStatusDisplay_, text.c_str());
    }
}

void UIManager::UpdatePrinterUIState(PrinterState state) {
    std::wstring stateText = L"TRẠNG THÁI: ";

    // SỬA: Sử dụng state.status thay vì state trực tiếp
    switch (state.status) {
    case PrinterStatus::Disconnected:
        stateText += L"NGẮT KẾT NỐI";
        break;
    case PrinterStatus::Connecting:
        stateText += L"ĐANG KẾT NỐI...";
        break;
    case PrinterStatus::Connected:
        stateText += L"ĐÃ KẾT NỐI";
        break;
    case PrinterStatus::Idle:
        stateText += L"SẴN SÀNG";
        break;
    case PrinterStatus::Printing:
        stateText += L"ĐANG IN";
        break;
    case PrinterStatus::Error:
        stateText += L"LỖI";
        break;
    default:
        stateText += L"KHÔNG XÁC ĐỊNH";
        break;
    }

    if (hPrinterState_) {
        SetWindowTextW(hPrinterState_, stateText.c_str());
    }
}

void UIManager::UpdateButtonStates(PrinterState state) {
    UpdateButtonStateForPrinterState(state);

    // Đồng bộ toggle với state thực tế
    bool shouldBeOn = state.IsConnected();
    if (isToggleOn_ != shouldBeOn) {
        SetToggleState(shouldBeOn);
    }
}

void UIManager::UpdateButtonStateForPrinterState(PrinterState state) {
    bool uploadEnabled = false;
    bool startEnabled = false;
    bool printEnabled = false;
    bool stopEnabled = false;
    bool clearEnabled = true;
    bool setEnabled = false;

    // SỬA: Sử dụng state.status thay vì state trực tiếp
    switch (state.status) {
    case PrinterStatus::Disconnected:
        uploadEnabled = true;
        setEnabled = true;
        break;
    case PrinterStatus::Connected:
    case PrinterStatus::Idle:
        uploadEnabled = true;
        startEnabled = true;
        printEnabled = true;
        stopEnabled = true;
        setEnabled = true;
        break;
    case PrinterStatus::Printing:
        uploadEnabled = true;
        printEnabled = true;
        stopEnabled = true;
        setEnabled = true;
        break;
    case PrinterStatus::Error:
        uploadEnabled = true;
        clearEnabled = true;
        break;
    default:
        break;
    }

    if (hBtnUpload_) EnableWindow(hBtnUpload_, uploadEnabled);
    if (btnStart_.GetHandle()) EnableWindow(btnStart_.GetHandle(), startEnabled);
    if (btnPrint_.GetHandle()) EnableWindow(btnPrint_.GetHandle(), printEnabled);
    if (btnStop_.GetHandle()) EnableWindow(btnStop_.GetHandle(), stopEnabled);
    if (btnClear_.GetHandle()) EnableWindow(btnClear_.GetHandle(), clearEnabled);
    if (btnSet_.GetHandle()) EnableWindow(btnSet_.GetHandle(), setEnabled);
}

void UIManager::SetToggleState(bool state) {
    isToggleOn_ = state;
    if (toggleSwitch_) {
        toggleSwitch_->SetState(state);
    }
}

bool UIManager::IsToggleOn() const {
    return isToggleOn_;
}

std::wstring UIManager::GetIPAddress() const {
    if (!hIpAddress_) return L"";

    DWORD ip = 0;
    SendMessage(hIpAddress_, IPM_GETADDRESS, 0, (LPARAM)&ip);
    BYTE a = (ip >> 0) & 0xFF;
    BYTE b = (ip >> 8) & 0xFF;
    BYTE c = (ip >> 16) & 0xFF;
    BYTE d = (ip >> 24) & 0xFF;

    wchar_t buf[32];
    swprintf_s(buf, L"%d.%d.%d.%d", d, c, b, a);
    return std::wstring(buf);
}

std::wstring UIManager::GetInputText() const {
    if (!hInputValue_) return L"";

    int len = GetWindowTextLengthW(hInputValue_);
    std::wstring s(len + 1, L'\0');
    GetWindowTextW(hInputValue_, &s[0], (int)s.size());
    s.resize(len);
    return s;
}

int UIManager::GetCountValue() const {
    if (!hCount_) return 1;

    wchar_t buf[16] = { 0 };
    GetWindowTextW(hCount_, buf, 16);
    return _wtoi(buf);
}

bool UIManager::ValidateInput() const {
    std::wstring ip = GetIPAddress();
    return !ip.empty() && ip != L"0.0.0.0" && ip != L"..";
}

void UIManager::HandleOwnerDraw(LPDRAWITEMSTRUCT dis) {
    if (!dis) return;

    switch (dis->CtlID) {
    case IDC_TOGGLE:
        if (toggleSwitch_) toggleSwitch_->Draw(dis);
        break;
    case IDC_BTN_START:
        btnStart_.Draw(dis);
        break;
    case IDC_BTN_PRINT:
        btnPrint_.Draw(dis);
        break;
    case IDC_BTN_STOP:
        btnStop_.Draw(dis);
        break;
    case IDC_BTN_SET:
        btnSet_.btnSetDraw(dis);
        break;
    case IDC_BTN_CLEAR:
        btnClear_.Draw(dis);
        break;
    default:
        break;
    }
}

void UIManager::ApplyFontToAllControls() {
    HFONT defaultFont = FontManager::GetInstance().GetDefaultFont();

    HWND allControls[] = {
        hIpAddress_, hInputValue_, hCount_, hStatusDisplay_, hPrinterState_,
        hBtnUpload_, hCurrentCount_
    };

    for (HWND hCtrl : allControls) {
        if (hCtrl && defaultFont) {
            SendMessage(hCtrl, WM_SETFONT, (WPARAM)defaultFont, TRUE);
        }
    }

    if (messageLogger_) {
        messageLogger_->SetFont(defaultFont);
    }
}

HWND UIManager::CreateStatic(int x, int y, int w, int h, const wchar_t* text) {
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, hParent_, NULL, NULL, NULL);
}

HWND UIManager::CreateButton(int x, int y, int w, int h, const wchar_t* text, int id) {
    return CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, hParent_, (HMENU)(UINT_PTR)id, NULL, NULL);
}

HWND UIManager::CreateEdit(int x, int y, int w, int h, const wchar_t* text) {
    return CreateWindowW(L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
        x, y, w, h, hParent_, NULL, NULL, NULL);
}