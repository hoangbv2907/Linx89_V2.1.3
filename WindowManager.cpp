
#include "WindowManager.h"
#include "Logger.h"
#include "UIManager.h"
#include <commctrl.h>
// ================== Lifecycle / Window creation ==================
WindowManager::WindowManager() {}

WindowManager::~WindowManager() {
    if (appController_) {
        appController_->ComprehensiveCleanup();
        appController_.reset();
    }
    // Cleanup UI components
    if (uiManager_) {
        uiManager_.reset();
    }
}

//đăng ký window class, lưu hInstance, init common stuff
bool WindowManager::Initialize(HINSTANCE hInstance) {
    hInstance_ = hInstance;     //lưu handle instance để dùng cho CreateWindowEx, RegisterClass
    // xác định loại common controls cần dùng.
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_INTERNET_CLASSES | ICC_STANDARD_CLASSES };
    //khởi tạo control trên Windows (button, edit box, progress bar…).
    InitCommonControlsEx(&icex);
    // tạo UIManager để quản lý toàn bộ control.
    uiManager_ = std::make_unique<UIManager>();
    Logger::GetInstance().Write(L"WindowManager initialized");
    return true;
}

// Tạo cửa sổ chính (CreateWindowEx) với title/size
bool WindowManager::CreateMainWindow(const std::wstring& title, int width, int height) {
    WNDCLASS wc = {};                               //khởi tạo struct WNDCLASS
    wc.lpfnWndProc = StaticWndProc;                 //chỉ định callback tĩnh để nhận message Windows.
    wc.hInstance = hInstance_;                      //instance của chương trình
    wc.lpszClassName = L"LinxMainWindow";           //tên class cửa sổ
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);    //con trỏ chuột mặc định
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);  //màu nền cửa sổ
    wc.style = CS_HREDRAW | CS_VREDRAW;             //vẽ lại khi thay đổi kích thước
    //đăng ký class, nếu fail → log và return false.
    if (!RegisterClass(&wc)) return false;
    //tạo cửa sổ chính với CreateWindowEx//Vị trí gọi: main.cpp → sau đó ShowWindow + UpdateWindow.
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc{ 0, 0, width, height };
    AdjustWindowRectEx(&rc, style, FALSE, 0);

    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    hwnd_ = CreateWindowEx(
        0, wc.lpszClassName, title.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
        nullptr, nullptr, hInstance_, this
    );
    if (!hwnd_) {
        Logger::GetInstance().Write(L"Failed to create main window", 2);
        return false;
    }
    return true;
}

// Entry point sau khi WndProc forward vào object instance
LRESULT WindowManager::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:     //tạo window
        HandleCreate();
        return 0;
    case WM_COMMAND: {  //xử lý lệnh từ button, menu, v.v.
        int id = LOWORD(wParam);
        HandleCommand(id);
        return 0;
    }
    case WM_DRAWITEM:   //vẽ nút custom (Owner-Draw button)
        HandleDrawItem(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
        return TRUE;

    case WM_APP_LOG: {  //nhận log message từ AppController thread, update UI.
        auto* log = (LogMessage*)wParam;
        if (uiManager_) uiManager_->AddMessage(log->text, log->level);
        delete log;
        return 0;
    }
    case WM_CLOSE:
    {
        try {
            appController_->SavePrintDataOnExit();
        }
        catch (const std::exception& e) {
        }
        DestroyWindow(hwnd_);
        return 0;
    }
    case WM_APP_PRINTER_UPDATE: {   //cập nhật trạng thái máy in trên UI
        PrinterStateMessage* stateMsg = reinterpret_cast<PrinterStateMessage*>(wParam);
        HandlePrinterUpdate(stateMsg);
        delete stateMsg;
        return 0;
    }
    case WM_APP_CONNECTION_UPDATE: {    //nhận thông tin connected/disconnected
        ConnectionMessage* msg = reinterpret_cast<ConnectionMessage*>(wParam);
        HandleConnectionUpdate(msg);   // dùng đúng hàm WindowManager đã viết
        delete msg;
        return 0;
    }
    case WM_APP_BUTTON_STATE: {     //cập nhật trạng thái button (active, disabled, running…)
        ButtonStateMessage* btnMsg = reinterpret_cast<ButtonStateMessage*>(wParam);
        HandleButtonState(btnMsg);
        delete btnMsg;
        return 0;
    }
    case WM_DESTROY:            //xử lý dọn dẹp khi cửa sổ bị đóng
        HandleDestroy();
        return 0;
    default:
        return DefWindowProc(hwnd_, msg, wParam, lParam);
    }
}

// WndProc tĩnh đăng ký với Windows
LRESULT CALLBACK WindowManager::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WindowManager* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);      // Lấy con trỏ instance từ lpCreateParams
        pThis = reinterpret_cast<WindowManager*>(cs->lpCreateParams);   // trỏ this của WindowManager
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);         // lưu con trỏ this vào dữ liệu cửa sổ     
        pThis->hwnd_ = hwnd;                                            // lưu HWND vào instance
        // Tạo AppController với HWND của cửa sổ chính
        pThis->appController_ = std::make_unique<AppController>(hwnd);
    }
    else {
        pThis = reinterpret_cast<WindowManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)); // Lấy con trỏ this từ dữ liệu cửa sổ
    }

    return pThis ? pThis->HandleMessage(msg, wParam, lParam)     // Chuyển message đến instance cụ thể
        : DefWindowProc(hwnd, msg, wParam, lParam);
}

// ================== Core WM_* handlers ==================
// ========================================================
// WM_CREATE: init UI controls (UIManager), bind state, load persisted data
void WindowManager::HandleCreate() {
    if (uiManager_) {
        Logger::GetInstance().Write(L"HandleCreate: Khởi tạo UI");
        uiManager_->Initialize(hwnd_);  // Khởi tạo UIManager với HWND của cửa sổ chính
        uiManager_->CreateControls();   // Tạo các control trên cửa sổ
        Logger::GetInstance().Write(L"HandleCreate: UI controls đã tạo");
        appController_->LoadPrintDataOnStart();
    }
    if (appController_) {   //kiểm tra AppController đã khởi tạo chưa
        Logger::GetInstance().Write(L"HandleCreate: Loading backup data");
        appController_->LoadPrintDataOnStart();
        appController_->SavePrintDataOnExit();
        Logger::GetInstance().Write(L"HandleCreate: Start worker thread");
        appController_->StartWorkerThread();
    }
}

// WM_COMMAND: dispatch theo control id (button/menu) sang OnXxxClicked()
void WindowManager::HandleCommand(int id) {
    switch (id) {
    case IDC_TOGGLE:        // nút toggle kết nối
        OnToggleClicked();
        break;
    case IDC_BTN_UPLOAD:    // nút upload nội dung
        OnUploadClicked();
        break;
    case IDC_BTN_START:     // nút start jet
        OnStartClicked();
        break;
    case IDC_BTN_PRINT:     // nút in
        OnPrintClicked();
        break;
    case IDC_BTN_STOP:      // nút dừng in
        OnStopClicked();
        break;
    case IDC_BTN_CLEAR:     // nút xóa log
        OnClearClicked();
        break;
    case IDC_BTN_SET:       // nút đặt số lượng in
        OnSetClicked();
        break;
    default:
        break;
    }
}

// WM_DRAWITEM: vẽ owner-draw button nếu UIManager dùng custom draw
void WindowManager::HandleDrawItem(LPDRAWITEMSTRUCT dis) {
    if (uiManager_ && dis) {    // kiểm tra UIManager và DRAWITEMSTRUCT không null
        uiManager_->HandleOwnerDraw(dis);   // chuyển yêu cầu vẽ đến UIManager
    }
}

// WM_DESTROY/WM_CLOSE: dọn dẹp, stop thread, save persist...
void WindowManager::HandleDestroy() {
    static std::atomic<bool> destroyHandled{ false };
    if (destroyHandled.exchange(true)) return;
    // ✅ DỪNG CONTROLLER TRƯỚC KHI POST QUIT
    if (appController_) {
        appController_->StopWorkerThread(1000);
    }
    PostQuitMessage(0);
}

// ================== AppController -> UI messages (WM_APP_*) ==================
// =============================================================================
// Nhận log từ worker thread và append vào log UI (MessageLogger/RichEdit)
void WindowManager::HandleAppLog(LogMessage* msg) {
    if (uiManager_ && msg) {    // kiểm tra UIManager và LogMessage không null
        uiManager_->AddMessage(msg->text, msg->level);  // thêm message vào log UI
    }
}

// Nhận snapshot trạng thái máy in (PrinterStateMessage) và cập nhật UI
void WindowManager::HandlePrinterUpdate(PrinterStateMessage* msg) {
    if (uiManager_ && msg) {
        uiManager_->UpdatePrinterStatus(msg->statusText);   // Cập nhật text trạng thái máy in
        uiManager_->UpdateJobFields(msg->jobContent, msg->targetCount, msg->printedCount);
        uiManager_->UpdatePrinterUIState(msg->state);       // Cập nhật trạng thái UI dựa trên trạng thái máy in
        uiManager_->UpdateButtonStates(msg->state);
    }
}

// Nhận trạng thái connected/disconnected và cập nhật UI toggle/state
void WindowManager::HandleConnectionUpdate(ConnectionMessage* msg) {
    if (uiManager_ && msg) {
        uiManager_->SetToggleState(msg->connected);     // Cập nhật trạng thái toggle      
        if (msg->connected) { // Thêm message kết nối/ngắt kết nối vào log UI
            uiManager_->AddMessage(L"✅ Đã kết nối đến " + msg->ipAddress, 4);
        }
        else
            uiManager_->AddMessage(L"X Đã ngắt kết nối với " + msg->ipAddress, 2);
    }
}

// Nhận state enable/disable/running của các nút và áp vào UI
void WindowManager::HandleButtonState(ButtonStateMessage* msg) {
    if (uiManager_ && msg) {
        uiManager_->UpdateButtonStates(msg->state);     // Cập nhật trạng thái button dựa trên trạng thái máy in
    }
}

// Các handler giao tiếp WindowManager -> AppController (enqueue request)
void WindowManager::OnToggleClicked() {
    if (!appController_ || !uiManager_) return;
    bool isToggleCurrentlyOn = uiManager_->IsToggleOn();
    if (!isToggleCurrentlyOn) { // User muốn KẾT NỐI      
        std::wstring ip = uiManager_->GetIPAddress();
        if (!uiManager_->ValidateInput()) {
            uiManager_->AddMessage(L"❌ Địa chỉ IP không hợp lệ", 2);
            return;
        }
        appController_->SetLastIp(ip);
        appController_->DisableAutoReconnect();
        appController_->Connect(ip);
    }
    else {
        uiManager_->SetToggleState(false);
        appController_->Disconnect();
    }
}

void WindowManager::OnUploadClicked() {
    if (!appController_ || !uiManager_) return;  // kiểm tra AppController và UIManager tồn tại
    std::wstring content = uiManager_->GetInputText();  // lấy nội dung từ UI
    if (content.empty()) {
        uiManager_->AddMessage(L"⚠️ Chưa có nội dung để tải lên", 1);
    }
    std::wstring fieldName = L"RemoteField1";
    appController_->UploadRemoteFieldData(fieldName, content);
    uiManager_->AddMessage(L"📤 Đang tải lên Remote Field: " + fieldName, 3);
}

void WindowManager::OnStartClicked() {
    if (!appController_) return;    // kiểm tra AppController tồn tại
    PrinterState cur = appController_->GetCurrentState();
    if (cur.jetOn) {
        uiManager_->AddMessage(L"⚠️ Jet đã ON — không gửi lệnh khởi động.", 3);
        return;
    }
    if (cur.status == PrinterStateType::StartingJet) {
        uiManager_->AddMessage(L"⚠️ Đang chờ Jet khởi động — vui lòng đợi.", 3);
        return;
    }
    appController_->StartJet(); //gọi StartJet trong AppController
    uiManager_->AddMessage(L"🚀 Khởi động jet...", 3);
}

void WindowManager::OnPrintClicked() {
    if (!appController_ || !uiManager_) return; // kiểm tra AppController tồn tại
    // Lấy trạng thái hiện tại từ AppController (hoặc PrinterModel)
    PrinterState cur = appController_->GetCurrentState();
    if (cur.status == PrinterStateType::Printing || cur.printing) {
        appController_->StopPrinting();
        uiManager_->AddMessage(L"⏸️ Yêu cầu tạm dừng in được gửi...", 3);
    }
    else {// Nếu không đang in -> bắt đầu in
        std::wstring content = uiManager_->GetInputText();
        int count = uiManager_->GetCountValue();
        if (!content.empty() && count > 0) {
            appController_->StartPrinting(content, count);
            uiManager_->AddMessage(L"🖨️ Đã gửi lệnh bắt đầu in...", 3);
        }
        else {
            uiManager_->AddMessage(L"⚠️ Chưa có nội dung hoặc số lượng không hợp lệ", 3);
        }
    }
}

void WindowManager::OnStopClicked() {
    if (!appController_) return;    // kiểm tra AppController tồn tại
    PrinterState cur = appController_->GetCurrentState();
    if (!cur.jetOn) {
        uiManager_->AddMessage(L"⚠️ Jet đã off — không gửi lệnh dừng", 3);
        return;
    }
    if (cur.status == PrinterStateType::StopingJet) {
        uiManager_->AddMessage(L"⚠️ Đang chờ dừng Jet  — vui lòng đợi.", 3);
        return;
    }
    appController_->StopJet(); //gọi StopPrinting trong AppController
    uiManager_->AddMessage(L"⏹️ Dừng jet...", 3);
}

void WindowManager::OnClearClicked() {
    if (uiManager_) {   // kiểm tra UIManager tồn tại
        uiManager_->ClearMessages();    // xóa tất cả message trong log UI
        uiManager_->AddMessage(L"Đã xóa nhật ký", 3);
    }
}

void WindowManager::OnSetClicked() {
    if (!appController_ || !uiManager_) return; // kiểm tra AppController và UIManager tồn tại
    int count = uiManager_->GetCountValue();    // lấy số lượng in từ UI
    if (count > 0) {
        appController_->SetCount(count); // gọi SetCount trong AppController
        appController_->SavePrintDataOnExit();
    }
    else {
        uiManager_->AddMessage(L"❌ Số lượng phải lớn hơn 0", 1);
    }
}