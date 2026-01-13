#include "UIManager.h"
#include "FontManager.h"
#include "Logger.h"
#include <commctrl.h>
#include <richedit.h>
#include <algorithm>
#include "CommonTypes.h"
// ================== Lifecycle ==================
UIManager::UIManager() {}

UIManager::~UIManager() {}

// ================== Init / Create UI ==================
bool UIManager::Initialize(HWND hParent) {
    hParent_ = hParent;
	// Đảm bảo rằng thư viện Rich Edit được tải
    LoadLibraryW(L"Msftedit.dll");
	// Khởi tạo các điều khiển tùy chỉnh
    toggleSwitch_ = std::make_unique<ToggleSwitch>();
    messageLogger_ = std::make_unique<MessageLogger>();
    return true;
}
// Tạo và cấu hình tất cả các điều khiển giao diện người dùng
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
        350, 140, 60, 22, hParent_,(HMENU)IDC_CURRENT_COUNT, NULL, NULL);
    // Modern buttons
    btnStart_.Create(hParent_, 20, 180, 140, 40, L"KHỞI ĐỘNG", IDC_BTN_START);
    btnPrint_.Create(hParent_, 180, 180, 140, 40, L"IN", IDC_BTN_PRINT);
    btnStop_.Create(hParent_, 340, 180, 140, 40, L"DỪNG", IDC_BTN_STOP);
    btnClear_.Create(hParent_, 380, 275, 100, 20, L"XÓA NHẬT KÝ", IDC_BTN_CLEAR);
    btnSet_.Create(hParent_, 420, 140, 60, 23, L"SET", IDC_BTN_SET);
    // Subclass các nút
    btnStart_.Subclass();
    btnPrint_.Subclass();
    btnStop_.Subclass();
    btnClear_.Subclass();
    btnSet_.Subclass();
    // Status display
    hStatusDisplay_ = CreateStatic(20, 240, 460, 24, L"CHƯA KẾT NỐI");
        SetWindowLongPtr(hStatusDisplay_, GWL_STYLE,
        GetWindowLongPtr(hStatusDisplay_, GWL_STYLE) | SS_CENTER);
    // Log box
    CreateStatic(20, 275, 100, 20, L"NHẬT KÝ:");
    messageLogger_->Create(hParent_, 20, 300, 460, 190);
    // Apply fonts
    ApplyFontToAllControls();
    // Set default IP
    SendMessage(hIpAddress_, IPM_SETADDRESS, 0, (LPARAM)MAKEIPADDRESS(127, 0, 0, 1));
    // Cập nhật giao diện theo trạng thái ban đầu
    PrinterState initialState;
    initialState.status = PrinterStateType::Disconnected;
    initialState.statusText = L"Chưa kết nối";
    UpdateButtonStates(initialState);
    UpdatePrinterUIState(initialState);
}

// ================== Log / Messaging ==================
// Append 1 dòng log vào MessageLogger. level dùng để tô màu/biểu tượng (info/warn/error...)
void UIManager::AddMessage(const std::wstring& text, int level) {
    if (messageLogger_) {
        messageLogger_->AddMessage(text, level);
    }
}
// Xóa toàn bộ log trong MessageLogger.
void UIManager::ClearMessages() {
	if (messageLogger_) {   // Kiểm tra con trỏ thông điệp nhật ký không rỗng trước khi sử dụng
		messageLogger_->Clear();    // Xóa tất cả thông điệp khỏi nhật ký
    }
}

// ================== UI updates from App state ==================
// Cập nhật text trạng thái (ví dụ: "Connected", "Printing", "Error: ...") lên label status.
void UIManager::UpdatePrinterStatus(const std::wstring& text) {
	if (hStatusDisplay_) {  // Kiểm tra handle của dòng trạng thái không rỗng trước khi sử dụng
		SetWindowTextW(hStatusDisplay_, text.c_str());  // Cập nhật văn bản của dòng trạng thái
    }
}

// Cập nhật "UI skin/state" theo trạng thái máy: đổi label, icon, màu nền, highlight...
void UIManager::UpdatePrinterUIState(PrinterState state){
    // Không làm gì nếu không đổi trạng thái
    bool statusChanged = (state.status != lastState_);
    lastState_ = state.status;
    std::wstring stateText = L"TRẠNG THÁI: ";
    COLORREF bgColor = RGB(240, 240, 240); // default xám
    switch (state.status)
    {
    case PrinterStateType::Disconnected:
        stateText += L"CHƯA KẾT NỐI";
        bgColor = RGB(240, 240, 240); // xám
        break;
    case PrinterStateType::Idle:
    case PrinterStateType::Connected:
        stateText += L"ĐÃ KẾT NỐI";
        bgColor = RGB(102, 205, 170); // xanh lá
        break;
    case PrinterStateType::Reconnecting:
    case PrinterStateType::Connecting:
        stateText += L"ĐANG KẾT NỐI...";
        bgColor = RGB(250, 230, 150); // vàng nhạt
        break;

    case PrinterStateType::Ready: // jetOn
        stateText += L"JET ON";
        bgColor = RGB(32, 178, 170); // xanh dương nhạt
        break;

    case PrinterStateType::Printing:
        stateText += L"ĐANG IN...";
        bgColor = RGB(150, 210, 255); // xanh dương nhạt
        break;

    case PrinterStateType::Error:
        stateText += L"LỖI MÁY IN";
        bgColor = RGB(255, 140, 140); // đỏ
        break;
    }
    // Cập nhật text trạng thái
    if (hPrinterState_)
        SetWindowTextW(hPrinterState_, stateText.c_str());
    // ⭐ TỐI ƯU: KHÔNG ĐỔI BRUSH NẾU MÀU KHÔNG ĐỔI
    static COLORREF lastColor = RGB(255, 255, 255);
    if (bgColor == lastColor) return;
    lastColor = bgColor;
    // Đổi nền cửa sổ
    HBRUSH brush = CreateSolidBrush(bgColor);
    SetClassLongPtr(hParent_, GCLP_HBRBACKGROUND, (LONG_PTR)brush);
    InvalidateRect(hParent_, NULL, TRUE);
}

// Wrapper để cập nhật enable/disable của các nút theo trạng thái máy.
void UIManager::UpdateButtonStates(PrinterState state) {
	UpdateButtonStateForPrinterState(state);    // Cập nhật trạng thái nút dựa trên trạng thái máy in
}

// Cập nhật các trường job: nội dung in, target count, printed count.
void UIManager::UpdateJobFields(const std::wstring& content, int target, int printed){
    // 0) Nếu user đang sửa và đã dirty, tuyệt đối không overwrite
    if (editingJobFields_ && jobDirtyByUser_) {
        // nhưng vẫn cho phép cập nhật printedCount nếu bạn muốn:
        if (hCurrentCount_ && printed != lastPrinted_) {
            lastPrinted_ = printed;
            SetWindowTextW(hCurrentCount_, std::to_wstring(printed).c_str());
        }
        return;
    }
    // 1) Nếu nội dung giống hệt lần trước thì bỏ qua để chống spam
    if (content == lastJobContent_ && target == lastTarget_ && printed == lastPrinted_) {
        return;
    }
    // 2) Chỉ SetWindowText cho field nào THẬT SỰ đổi
    if (hInputValue_ && content != lastJobContent_) {
        lastJobContent_ = content;
        SetWindowTextW(hInputValue_, content.c_str());
    }
    if (hCount_ && target != lastTarget_) {
        lastTarget_ = target;
        SetWindowTextW(hCount_, std::to_wstring(target).c_str());
    }
    if (hCurrentCount_ && printed != lastPrinted_) {
        lastPrinted_ = printed;
        SetWindowTextW(hCurrentCount_, std::to_wstring(printed).c_str());
    }
}

// ================== Toggle switch ==================
// Set ON/OFF cho toggle trên UI (ví dụ: Connected = ON).
void UIManager::SetToggleState(bool state) {
    Logger::GetInstance().Write(L"UIManager::SetToggleState: " +
        std::wstring(state ? L"ON" : L"OFF") +
        L", toggleSwitch_ exists: " + std::wstring(toggleSwitch_ ? L"YES" : L"NO"));
    isToggleOn_ = state;    // Cập nhật trạng thái bên trong
    if (toggleSwitch_) {    // Kiểm tra con trỏ toggle không rỗng trước khi sử dụng
        toggleSwitch_->SetState(state); // Cập nhật trạng thái của toggle switch
    }
}

// Đọc trạng thái toggle hiện tại (UI state) - không phải trạng thái socket thật.
bool UIManager::IsToggleOn() const {
    return isToggleOn_;
}

// ================== Data access (read user inputs) ==================
// Lấy IP từ edit box (hIpAddress_).
std::wstring UIManager::GetIPAddress() const {
	if (!hIpAddress_) return L"";   // Kiểm tra handle của điều khiển IP Address không rỗng trước khi sử dụng
	DWORD ip = 0;   // Biến để lưu địa chỉ IP
	SendMessage(hIpAddress_, IPM_GETADDRESS, 0, (LPARAM)&ip);   // Lấy địa chỉ IP từ điều khiển
	BYTE a = (ip >> 0) & 0xFF;  // Trích xuất từng byte của địa chỉ IP
    BYTE b = (ip >> 8) & 0xFF;
    BYTE c = (ip >> 16) & 0xFF;
    BYTE d = (ip >> 24) & 0xFF;
	wchar_t buf[32];    // Bộ đệm để định dạng địa chỉ IP
	swprintf_s(buf, L"%d.%d.%d.%d", d, c, b, a);    // Định dạng địa chỉ IP thành chuỗi
	return std::wstring(buf);   // Trả về địa chỉ IP dưới dạng chuỗi
}

// Lấy nội dung từ edit box nội dung in (hInputValue_).
std::wstring UIManager::GetInputText() const {
	if (!hInputValue_) return L"";  // Kiểm tra handle của điều khiển nhập liệu không rỗng trước khi sử dụng
	int len = GetWindowTextLengthW(hInputValue_);   // Lấy độ dài văn bản trong điều khiển
	std::wstring s(len + 1, L'\0'); // Tạo chuỗi với kích thước đủ lớn để chứa văn bản
	GetWindowTextW(hInputValue_, &s[0], (int)s.size()); // Lấy văn bản từ điều khiển
	s.resize(len);  // Thay đổi kích thước chuỗi để loại bỏ ký tự null thừa
	return s;   // Trả về văn bản nhập dưới dạng chuỗi
}

// Lấy số lượng in từ edit box count (hCount_).
int UIManager::GetCountValue() const {
	if (!hCount_) return 1; // Kiểm tra handle của điều khiển số lượng không rỗng trước khi sử dụng
	wchar_t buf[16] = { 0 };    // Bộ đệm để lưu văn bản số lượng
	GetWindowTextW(hCount_, buf, 16);   // Lấy văn bản từ điều khiển số lượng
	return _wtoi(buf);  // Chuyển đổi văn bản thành số nguyên và trả về
}

// Kiểm tra dữ liệu nhập hợp lệ (IP đúng định dạng, count > 0...)
bool UIManager::ValidateInput() const {
	std::wstring ip = GetIPAddress();   // Lấy địa chỉ IP từ điều khiển
	return !ip.empty() && ip != L"0.0.0.0" && ip != L"..";  // Kiểm tra địa chỉ IP không rỗng và không phải là địa chỉ mặc định không hợp lệ
}

// Xử lý WM_DRAWITEM: vẽ các controls kiểu owner-draw (ModernButton/ToggleSwitch).
    // WindowManager sẽ gọi hàm này từ HandleDrawItem().
void UIManager::HandleOwnerDraw(LPDRAWITEMSTRUCT dis) {
	if (!dis) return;   // Kiểm tra con trỏ DRAWITEMSTRUCT không rỗng trước khi sử dụng
	// Xác định điều khiển dựa trên CtlID và gọi hàm vẽ tương ứng
    switch (dis->CtlID) {
    case IDC_TOGGLE:
		if (toggleSwitch_) toggleSwitch_->Draw(dis);    // Vẽ toggle switch
        break;
	case IDC_BTN_START: // Vẽ nút Start
        btnStart_.Draw(dis);
        break;
	case IDC_BTN_PRINT: // Vẽ nút Print
        btnPrint_.Draw(dis);
        break;  
	case IDC_BTN_STOP:  // Vẽ nút Stop
        btnStop_.Draw(dis);
        break;
	case IDC_BTN_SET:   // Vẽ nút Set
        btnSet_.btnSetDraw(dis);
        break;  
	case IDC_BTN_CLEAR: // Vẽ nút Clear
        btnClear_.Draw(dis);
        break;
    default:
        break;
    }
}

// ================== Helper methods (control creation / layout) ==================
// Tạo static label đơn giản (WS_CHILD | WS_VISIBLE)
HWND UIManager::CreateStatic(int x, int y, int w, int h, const wchar_t* text) {
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, hParent_, NULL, NULL, NULL);
}

// Tạo button chuẩn Win32 (nếu không dùng ModernButton cho nút đó)
HWND UIManager::CreateButton(int x, int y, int w, int h, const wchar_t* text, int id) {
    return CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, hParent_, (HMENU)(UINT_PTR)id, NULL, NULL);
}

// Tạo edit box (cho IP/content/count). text mặc định trống.
HWND UIManager::CreateEdit(int x, int y, int w, int h, const wchar_t* text) {
    return CreateWindowW(L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
        x, y, w, h, hParent_, NULL, NULL, NULL);
}

// Apply font thống nhất lên tất cả controls để UI đồng bộ.
void UIManager::ApplyFontToAllControls() {
    HFONT defaultFont = FontManager::GetInstance().GetDefaultFont();    // Lấy font mặc định từ FontManager
    HWND allControls[] = {
        hIpAddress_, hInputValue_, hCount_, hStatusDisplay_, hPrinterState_,
        hBtnUpload_, hCurrentCount_
    };
    // Áp dụng font cho từng điều khiển trong danh sách
    for (HWND hCtrl : allControls) {
        if (hCtrl && defaultFont) {
            SendMessage(hCtrl, WM_SETFONT, (WPARAM)defaultFont, TRUE);
        }
    }
    if (messageLogger_) {
        messageLogger_->SetFont(defaultFont);
    }
}

// Core logic enable/disable từng nút theo PrinterState:
void UIManager::UpdateButtonStateForPrinterState(PrinterState state) {
    // 🔒 HARD LOCK: Jet đang chuyển trạng thái
    if (state.jetTransitioning)
    {
        EnableWindow(btnStart_.GetHandle(), FALSE);
        EnableWindow(btnStop_.GetHandle(), FALSE);
        EnableWindow(btnPrint_.GetHandle(), FALSE);
        EnableWindow(hBtnUpload_, FALSE);
        EnableWindow(btnSet_.GetHandle(), FALSE);
        return; // ⛔ TUYỆT ĐỐI KHÔNG CHẠY LOGIC DƯỚI
    }
    // ===== logic bình thường =====
    bool upload = false;
    bool startJet = false;
    bool stopJet = false;
    bool print = false;
    bool stopPrint = false;
    bool setCount = false;
    switch (state.status)
    {
    case PrinterStateType::Disconnected:
    case PrinterStateType::Connecting:
    case PrinterStateType::Error:
        break;
    case PrinterStateType::Idle:
        upload = true;
        startJet = true;
        setCount = true;
        btnPrint_.SetCaption(L"IN");
        btnPrint_.SetBaseColor(RGB(200, 200, 200));
        break;
    case PrinterStateType::Ready:
        upload = true;
        startJet = false;
        stopJet = true;
        print = true;
        stopPrint = true;
        setCount = true;
        btnPrint_.SetCaption(L"BẮT ĐẦU IN");
        btnPrint_.SetBaseColor(RGB(0, 180, 0));
        break;
    case PrinterStateType::Printing:
        stopJet = true;
        print = true;
        stopPrint = true;
        setCount = false;
        btnPrint_.SetCaption(L"TẠM DỪNG IN");
        btnPrint_.SetBaseColor(RGB(255, 160, 0));
        break;
    }
    EnableWindow(hBtnUpload_, upload);
    EnableWindow(btnStart_.GetHandle(), startJet);
    EnableWindow(btnPrint_.GetHandle(), print || stopPrint);
    EnableWindow(btnStop_.GetHandle(), stopJet);
    EnableWindow(btnSet_.GetHandle(), setCount);
}
