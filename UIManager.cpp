#include "UIManager.h"
#include "FontManager.h"
#include "Logger.h"
#include <commctrl.h>
#include <richedit.h>
#include <algorithm>
#include "CommonTypes.h"

UIManager::UIManager() {}

UIManager::~UIManager() {}

//Lưu handle của cửa sổ cha vào biến thành viên hParent_.
bool UIManager::Initialize(HWND hParent) {
    hParent_ = hParent;

	// Đảm bảo rằng thư viện Rich Edit được tải
    LoadLibraryW(L"Msftedit.dll");

	// Khởi tạo các điều khiển tùy chỉnh
    toggleSwitch_ = std::make_unique<ToggleSwitch>();
    messageLogger_ = std::make_unique<MessageLogger>();

    Logger::GetInstance().Write(L"UIManager initialized");
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

    // Cập nhật giao diện theo trạng thái ban đầu
    PrinterState initialState;
    initialState.status = PrinterStateType::Disconnected;
    initialState.statusText = L"Chưa kết nối";
    UpdateButtonStates(initialState);

    Logger::GetInstance().Write(L"UI controls created");
}

// Thêm một thông điệp vào nhật ký hiển thị trong giao diện người dùng
void UIManager::AddMessage(const std::wstring& text) {
	if (messageLogger_) {   // Kiểm tra con trỏ thông điệp nhật ký không rỗng trước khi sử dụng
		messageLogger_->AddMessage(text);   // Thêm thông điệp vào nhật ký
    }
}

// Xóa tất cả thông điệp khỏi nhật ký hiển thị trong giao diện người dùng
void UIManager::ClearMessages() {
	if (messageLogger_) {   // Kiểm tra con trỏ thông điệp nhật ký không rỗng trước khi sử dụng
		messageLogger_->Clear();    // Xóa tất cả thông điệp khỏi nhật ký
    }
}

// Cập nhật dòng trạng thái của máy in trong giao diện người dùng
void UIManager::UpdatePrinterStatus(const std::wstring& text) {
	if (hStatusDisplay_) {  // Kiểm tra handle của dòng trạng thái không rỗng trước khi sử dụng
		SetWindowTextW(hStatusDisplay_, text.c_str());  // Cập nhật văn bản của dòng trạng thái
    }
}

// Cập nhật trạng thái UI của máy in dựa trên trạng thái hiện tại
void UIManager::UpdatePrinterUIState(PrinterState state) {
    
    if (state.status == lastState_) {
        return; // Không đổi màu nếu trạng thái không đổi
    }

    lastState_ = state.status;

    std::wstring stateText = L"TRẠNG THÁI: ";

    COLORREF bgColor = RGB(240, 240, 240); // default

    switch (state.status) {
    case PrinterStateType::Disconnected:
        stateText += L"NGẮT KẾT NỐI";
        bgColor = RGB(240, 240, 240); // xám nhạt
        break;

    case PrinterStateType::Connecting:
        stateText += L"ĐANG KẾT NỐI...";
        bgColor = RGB(255, 255, 180); // vàng nhạt
        break;

    case PrinterStateType::Connected:
        stateText += L"ĐÃ KẾT NỐI";
        bgColor = RGB(200, 255, 200); // xanh nhạt
        break;

    case PrinterStateType::Idle:
        stateText += L"SẴN SÀNG";
        bgColor = RGB(200, 255, 200); // xanh nhạt
        break;

    case PrinterStateType::Printing:
        stateText += L"ĐANG IN";
        bgColor = RGB(180, 240, 255); // xanh lơ nhạt
        break;

    case PrinterStateType::Error:
        stateText += L"LỖI";
        bgColor = RGB(255, 180, 180); // đỏ nhạt
        break;
    }

    // Cập nhật chữ trạng thái
    if (hPrinterState_)
        SetWindowTextW(hPrinterState_, stateText.c_str());

    // === THÊM ĐỔI NỀN TOÀN CỬA SỔ ===
    HBRUSH brush = CreateSolidBrush(bgColor);
    SetClassLongPtr(hParent_, GCLP_HBRBACKGROUND, (LONG_PTR)brush);
    InvalidateRect(hParent_, NULL, TRUE);  // yêu cầu vẽ lại
}


// Cập nhật trạng thái các nút dựa trên trạng thái hiện tại của máy in
void UIManager::UpdateButtonStates(PrinterState state) {
	UpdateButtonStateForPrinterState(state);    // Cập nhật trạng thái nút dựa trên trạng thái máy in
    
    // Đồng bộ toggle với state thực tế
    bool shouldBeOn = (state.status == PrinterStateType::Connected || state.status == PrinterStateType::Connecting);
    // Toggle nên bật nếu máy in đã kết nối
	if (isToggleOn_ != shouldBeOn) {    // Nếu trạng thái toggle không khớp với trạng thái thực tế
        Logger::GetInstance().Write(L"Toggle state changing: " +
            std::to_wstring(isToggleOn_) + L" -> " + std::to_wstring(shouldBeOn));
        SetToggleState(shouldBeOn);     // Cập nhật trạng thái toggle
    }
}

// Cập nhật trạng thái của các nút dựa trên trạng thái hiện tại của máy in
void UIManager::UpdateButtonStateForPrinterState(PrinterState state) {
	// Khởi tạo tất cả trạng thái nút là false
    bool uploadEnabled = false;
    bool startEnabled = false;
    bool printEnabled = false;
    bool stopEnabled = false;
    bool clearEnabled = true;
    bool setEnabled = false;

	// Xác định trạng thái nút dựa trên trạng thái máy in
    switch (state.status) {
    case PrinterStateType::Disconnected:
        uploadEnabled = true;
        setEnabled = true;
        break;
    case PrinterStateType::Connected:
    case PrinterStateType::Idle:
        uploadEnabled = true;
        startEnabled = true;
        printEnabled = true;
        stopEnabled = true;
        setEnabled = true;
        break;
    case PrinterStateType::Printing:
        uploadEnabled = true;
        printEnabled = true;
        stopEnabled = true;
        setEnabled = true;
        break;
    case PrinterStateType::Error:
        uploadEnabled = true;
        clearEnabled = true;
        break;
    default:
        break;
    }
	// Cập nhật trạng thái nút trong giao diện người dùng
    if (hBtnUpload_) EnableWindow(hBtnUpload_, uploadEnabled); 
    if (btnStart_.GetHandle()) EnableWindow(btnStart_.GetHandle(), startEnabled);   
    if (btnPrint_.GetHandle()) EnableWindow(btnPrint_.GetHandle(), printEnabled);
    if (btnStop_.GetHandle()) EnableWindow(btnStop_.GetHandle(), stopEnabled);
    if (btnClear_.GetHandle()) EnableWindow(btnClear_.GetHandle(), clearEnabled);
    if (btnSet_.GetHandle()) EnableWindow(btnSet_.GetHandle(), setEnabled);
}

// Cài đặt trạng thái của toggle switch và đồng bộ giao diện
void UIManager::SetToggleState(bool state) {
    // THÊM: Debug logging
    Logger::GetInstance().Write(L"UIManager::SetToggleState: " +
        std::wstring(state ? L"ON" : L"OFF") +
        L", toggleSwitch_ exists: " + std::wstring(toggleSwitch_ ? L"YES" : L"NO"));

	isToggleOn_ = state;    // Cập nhật trạng thái bên trong
	if (toggleSwitch_) {    // Kiểm tra con trỏ toggle không rỗng trước khi sử dụng
		toggleSwitch_->SetState(state); // Cập nhật trạng thái của toggle switch
        
    }
}

// Trả về trạng thái hiện tại của toggle switch
bool UIManager::IsToggleOn() const {
    return isToggleOn_;
}

// Lấy địa chỉ IP từ điều khiển IP Address và trả về dưới dạng chuỗi
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

// Lấy văn bản nhập từ điều khiển nhập liệu và trả về dưới dạng chuỗi
std::wstring UIManager::GetInputText() const {
	if (!hInputValue_) return L"";  // Kiểm tra handle của điều khiển nhập liệu không rỗng trước khi sử dụng

	int len = GetWindowTextLengthW(hInputValue_);   // Lấy độ dài văn bản trong điều khiển
	std::wstring s(len + 1, L'\0'); // Tạo chuỗi với kích thước đủ lớn để chứa văn bản
	GetWindowTextW(hInputValue_, &s[0], (int)s.size()); // Lấy văn bản từ điều khiển
	s.resize(len);  // Thay đổi kích thước chuỗi để loại bỏ ký tự null thừa
	return s;   // Trả về văn bản nhập dưới dạng chuỗi
}

// Lấy giá trị số lượng từ điều khiển nhập liệu và trả về dưới dạng số nguyên
int UIManager::GetCountValue() const {
	if (!hCount_) return 1; // Kiểm tra handle của điều khiển số lượng không rỗng trước khi sử dụng

	wchar_t buf[16] = { 0 };    // Bộ đệm để lưu văn bản số lượng
	GetWindowTextW(hCount_, buf, 16);   // Lấy văn bản từ điều khiển số lượng
	return _wtoi(buf);  // Chuyển đổi văn bản thành số nguyên và trả về
}

// Xác thực đầu vào địa chỉ IP
bool UIManager::ValidateInput() const {
	std::wstring ip = GetIPAddress();   // Lấy địa chỉ IP từ điều khiển
	return !ip.empty() && ip != L"0.0.0.0" && ip != L"..";  // Kiểm tra địa chỉ IP không rỗng và không phải là địa chỉ mặc định không hợp lệ
}

// Xử lý vẽ tùy chỉnh cho các điều khiển owner-draw
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

// Áp dụng font mặc định cho tất cả các điều khiển giao diện người dùng
void UIManager::ApplyFontToAllControls() {
	HFONT defaultFont = FontManager::GetInstance().GetDefaultFont();    // Lấy font mặc định từ FontManager

	// Danh sách tất cả các điều khiển cần áp dụng font
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

// Tạo một điều khiển tĩnh (static control)
HWND UIManager::CreateStatic(int x, int y, int w, int h, const wchar_t* text) {
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, hParent_, NULL, NULL, NULL);
}
// Tạo một nút bấm (button control)
HWND UIManager::CreateButton(int x, int y, int w, int h, const wchar_t* text, int id) {
    return CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, hParent_, (HMENU)(UINT_PTR)id, NULL, NULL);
}
// Tạo một điều khiển chỉnh sửa văn bản (edit control)
HWND UIManager::CreateEdit(int x, int y, int w, int h, const wchar_t* text) {
    return CreateWindowW(L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
        x, y, w, h, hParent_, NULL, NULL, NULL);
}