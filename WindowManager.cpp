#include "WindowManager.h"
#include "Logger.h"
#include <commctrl.h>

WindowManager::WindowManager() {}

WindowManager::~WindowManager() {
    Logger::GetInstance().Write(L"WindowManager shutdown started");

    // Gọi comprehensive cleanup
    if (appController_) {
        appController_->ComprehensiveCleanup();
    }

    // Cleanup UI components
    if (uiManager_) {
        uiManager_.reset();
    }

    Logger::GetInstance().Write(L"WindowManager shutdown completed");
}

bool WindowManager::Initialize(HINSTANCE hInstance) {
    hInstance_ = hInstance;

    // Initialize common controls
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_INTERNET_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);

    // Create UI manager
    uiManager_ = std::make_unique<UIManager>();

    Logger::GetInstance().Write(L"WindowManager initialized");
    return true;
}

bool WindowManager::CreateMainWindow(const std::wstring& title, int width, int height) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hInstance_;
    wc.lpszClassName = L"LinxMainWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;

    if (!RegisterClass(&wc)) {
        Logger::GetInstance().Write(L"Failed to register window class", 2);
        return false;
    }

    hwnd_ = CreateWindowEx(
        0, wc.lpszClassName, title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        nullptr, nullptr, hInstance_, this
    );

    if (!hwnd_) {
        Logger::GetInstance().Write(L"Failed to create main window", 2);
        return false;
    }

    return true;
}

LRESULT CALLBACK WindowManager::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WindowManager* pThis = nullptr;

    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<WindowManager*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->hwnd_ = hwnd;

        // Create app controller after window is created
        pThis->appController_ = std::make_unique<AppController>(hwnd);
    }
    else {
        pThis = reinterpret_cast<WindowManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    return pThis ? pThis->HandleMessage(msg, wParam, lParam)
        : DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT WindowManager::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        HandleCreate();
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        HandleCommand(id);
        return 0;
    }

    case WM_DRAWITEM:
        HandleDrawItem(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
        return TRUE;

        // Thread-safe UI updates from background threads
    case WM_APP_LOG: {
        LogMessage* logMsg = reinterpret_cast<LogMessage*>(wParam);
        HandleAppLog(logMsg);
        delete logMsg;
        return 0;
    }

    case WM_APP_PRINTER_UPDATE: {
        PrinterStateMessage* stateMsg = reinterpret_cast<PrinterStateMessage*>(wParam);
        HandlePrinterUpdate(stateMsg);
        delete stateMsg;
        return 0;
    }

    case WM_APP_CONNECTION_UPDATE: {
        ConnectionMessage* connMsg = reinterpret_cast<ConnectionMessage*>(wParam);
        HandleConnectionUpdate(connMsg);
        delete connMsg;
        return 0;
    }

    case WM_APP_BUTTON_STATE: {
        ButtonStateMessage* btnMsg = reinterpret_cast<ButtonStateMessage*>(wParam);
        HandleButtonState(btnMsg);
        delete btnMsg;
        return 0;
    }

    case WM_DESTROY:
        if (appController_) {
            appController_->StopWorkerThread(200); // SỬA: StopWorkerThread thay vì StopBackgroundThreads
        }
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd_, msg, wParam, lParam);
    }
}

void WindowManager::HandleCreate() {
    if (uiManager_) {
        uiManager_->Initialize(hwnd_);
        uiManager_->CreateControls();
    }

    if (appController_) {
        appController_->StartWorkerThread(); // SỬA: StartWorkerThread thay vì StartBackgroundThreads
    }
}

void WindowManager::HandleCommand(int id) {
    switch (id) {
    case IDC_TOGGLE:
        OnToggleClicked();
        break;
    case IDC_BTN_UPLOAD:
        OnUploadClicked();
        break;
    case IDC_BTN_START:
        OnStartClicked();
        break;
    case IDC_BTN_PRINT:
        OnPrintClicked();
        break;
    case IDC_BTN_STOP:
        OnStopClicked();
        break;
    case IDC_BTN_CLEAR:
        OnClearClicked();
        break;
    case IDC_BTN_SET:
        OnSetClicked();
        break;
    default:
        break;
    }
}

void WindowManager::HandleDrawItem(LPDRAWITEMSTRUCT dis) {
    if (uiManager_ && dis) {
        uiManager_->HandleOwnerDraw(dis);
    }
}

void WindowManager::HandleAppLog(LogMessage* msg) {
    if (uiManager_ && msg) {
        uiManager_->AddMessage(msg->text);
    }
}

void WindowManager::HandlePrinterUpdate(PrinterStateMessage* msg) {
    if (uiManager_ && msg) {
        uiManager_->UpdatePrinterStatus(msg->statusText);
        uiManager_->UpdatePrinterUIState(msg->state);
    }
}

void WindowManager::HandleConnectionUpdate(ConnectionMessage* msg) {
    if (uiManager_ && msg) {
        uiManager_->SetToggleState(msg->connected);
        if (msg->connected) {
            uiManager_->AddMessage(L"✅ Đã kết nối đến " + msg->ipAddress);
        }
        else {
            uiManager_->AddMessage(L"🔌 Đã ngắt kết nối");
        }
    }
}

void WindowManager::HandleButtonState(ButtonStateMessage* msg) {
    if (uiManager_ && msg) {
        uiManager_->UpdateButtonStates(msg->state);
    }
}

// Thêm vào WindowManager để xử lý WM_DESTROY và exceptions
void WindowManager::HandleDestroy() {
    Logger::GetInstance().Write(L"Main window destruction - starting cleanup");

    static bool cleanupDone = false;
    if (!cleanupDone) {
        cleanupDone = true;

        if (appController_) {
            // Thử graceful cleanup trước
            appController_->ComprehensiveCleanup();
        }
    }

    PostQuitMessage(0);
}
// UI Interaction Handlers
void WindowManager::OnToggleClicked() {
    if (!appController_ || !uiManager_) return;

    // SỬA: Kiểm tra trạng thái kết nối đúng cách
    PrinterState currentState = appController_->GetCurrentState();
    if (currentState.status == PrinterStatus::Disconnected) {
        std::wstring ip = uiManager_->GetIPAddress();
        if (uiManager_->ValidateInput()) {
            appController_->Connect(ip); // SỬA: Gọi Connect thay vì OnToggleConnection
            uiManager_->AddMessage(L"🔄 Đang kết nối đến " + ip);
        }
        else {
            uiManager_->AddMessage(L"❌ Địa chỉ IP không hợp lệ");
            uiManager_->SetToggleState(false); // Reset toggle về off
        }
    }
    else {
        appController_->Disconnect(); // SỬA: Gọi Disconnect thay vì OnToggleConnection
        uiManager_->AddMessage(L"🔌 Đang ngắt kết nối...");
    }
}

void WindowManager::OnUploadClicked() {
    if (!appController_ || !uiManager_) return;

    std::wstring content = uiManager_->GetInputText();
    if (!content.empty()) {
        uiManager_->AddMessage(L"📤 Đã tải lên nội dung: " + content);
    }
    else {
        uiManager_->AddMessage(L"⚠️ Chưa có nội dung để tải lên");
    }
}

void WindowManager::OnStartClicked() {
    if (!appController_) return;

    appController_->StartJet(); // SỬA: Gọi StartJet thay vì OnStartPrinting
    uiManager_->AddMessage(L"🚀 Khởi động jet...");
}

void WindowManager::OnPrintClicked() {
    if (!appController_ || !uiManager_) return;

    std::wstring content = uiManager_->GetInputText();
    int count = uiManager_->GetCountValue();

    if (!content.empty()) {
        appController_->StartPrinting(content, count); // SỬA: Gọi StartPrinting thay vì OnStartPrinting
        uiManager_->AddMessage(L"🖨️ Bắt đầu in...");
    }
    else {
        uiManager_->AddMessage(L"⚠️ Chưa có nội dung để in");
    }
}

void WindowManager::OnStopClicked() {
    if (!appController_) return;

    appController_->StopPrinting(); // SỬA: Gọi StopPrinting thay vì OnStopPrinting
    uiManager_->AddMessage(L"⏹️ Dừng in...");
}

void WindowManager::OnClearClicked() {
    if (uiManager_) {
        uiManager_->ClearMessages();
        uiManager_->AddMessage(L"Đã xóa nhật ký");
    }
}

void WindowManager::OnSetClicked() {
    if (!appController_ || !uiManager_) return;

    int count = uiManager_->GetCountValue();
    if (count > 0) {
        appController_->SetCount(count); // SỬA: Gọi SetCount thay vì OnSetCount
        uiManager_->AddMessage(L"Đã đặt số lượng in: " + std::to_wstring(count));
    }
    else {
        uiManager_->AddMessage(L"❌ Số lượng phải lớn hơn 0");
    }
}