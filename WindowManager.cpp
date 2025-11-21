#include "WindowManager.h"
#include "Logger.h"
#include <commctrl.h>

// Constructor
WindowManager::WindowManager() {}

// Destructor
WindowManager::~WindowManager() {
    Logger::GetInstance().Write(L"WindowManager shutdown started");

    // Gọi comprehensive cleanup
    if (appController_) {
        appController_->EmergencyCleanup();
        appController_.reset();
    }

    // Cleanup UI components
    if (uiManager_) {
        uiManager_.reset();
    }

    Logger::GetInstance().Write(L"WindowManager shutdown completed");
}

//vị trí gọi từ main.cpp ngay sau khi main khởi chạy/ chuẩn bị WindowManager trước khi tạo window
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

//vị trí gọi từ main.cpp ngay sau khi Initialize → tạo cửa sổ chính.//đăng ký class của cửa sổ
bool WindowManager::CreateMainWindow(const std::wstring& title, int width, int height) {
	WNDCLASS wc = {};                               //khởi tạo struct WNDCLASS
    wc.lpfnWndProc = StaticWndProc;                 //chỉ định callback tĩnh để nhận message Windows.
    wc.hInstance = hInstance_;                      //instance của chương trình
	wc.lpszClassName = L"LinxMainWindow";           //tên class cửa sổ
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);    //con trỏ chuột mặc định
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);  //màu nền cửa sổ
	wc.style = CS_HREDRAW | CS_VREDRAW;             //vẽ lại khi thay đổi kích thước

    //đăng ký class, nếu fail → log và return false.
    if (!RegisterClass(&wc)) {
        Logger::GetInstance().Write(L"Failed to register window class", 2);
        return false;
    }

	//tạo cửa sổ chính với CreateWindowEx//Vị trí gọi: main.cpp → sau đó ShowWindow + UpdateWindow.
    hwnd_ = CreateWindowEx(
		0, wc.lpszClassName, title.c_str(),             // tiêu đề cửa sổ
		WS_OVERLAPPEDWINDOW,                            // kiểu cửa sổ chuẩn                
		CW_USEDEFAULT, CW_USEDEFAULT, width, height,    // vị trí và kích thước
		nullptr, nullptr, hInstance_, this              // không có menu, truyền vào lpCreateParams → dùng trong WM_NCCREATE để StaticWndProc biết con trỏ instance
    );

    if (!hwnd_) {
        Logger::GetInstance().Write(L"Failed to create main window", 2);
        return false;
    }

    return true;
}

// Static WndProc để chuyển message đến instance cụ thể, Windows gọi khi có bất kỳ message nào tới hwnd.
// vị trí gọi: mọi message từ windows → chuyển đến hàm xử lý tương ứng.
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

//vị trí gọi: mọi mesage từ windows → chuyển đến hàm xử lý tương ứng. Xử lý message Windows
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
        LogMessage* logMsg = reinterpret_cast<LogMessage*>(wParam);
        HandleAppLog(logMsg);
        delete logMsg;
        return 0;
    }

    case WM_APP_PRINTER_UPDATE: {   //cập nhật trạng thái máy in trên UI
        PrinterStateMessage* stateMsg = reinterpret_cast<PrinterStateMessage*>(wParam);
        HandlePrinterUpdate(stateMsg);

        delete stateMsg;
        return 0;
    }

    case WM_APP_CONNECTION_UPDATE: {    //nhận thông tin connected/disconnected
        ConnectionMessage* connMsg = reinterpret_cast<ConnectionMessage*>(wParam);
        HandleConnectionUpdate(connMsg);
        delete connMsg;
        return 0;
    }

    case WM_APP_BUTTON_STATE: {     //cập nhật trạng thái button (active, disabled, running…)
        ButtonStateMessage* btnMsg = reinterpret_cast<ButtonStateMessage*>(wParam);
        HandleButtonState(btnMsg);
        delete btnMsg;
        return 0;
    }

    case WM_DESTROY:            //xử lý dọn dẹp khi cửa sổ bị đóng
        if (appController_) {
            appController_->StopWorkerThread(200);
            PostQuitMessage(0);
            return 0;

    default:
        return DefWindowProc(hwnd_, msg, wParam, lParam);
        }
    }
}

//Vị trí gọi: WM_CREATE. tạo control và khởi động thread AppController.
void WindowManager::HandleCreate() {
	if (uiManager_) {   //kiểm tra UIManager đã khởi tạo chưa
		uiManager_->Initialize(hwnd_);  // Khởi tạo UIManager với HWND của cửa sổ chính
		uiManager_->CreateControls();   // Tạo các control trên cửa sổ
    }

	if (appController_) {   //kiểm tra AppController đã khởi tạo chưa
		appController_->StartWorkerThread(); // Bắt đầu thread làm việc trong AppController
    }
}

//Vị trí gọi: WM_COMMAND. map từng nút click → hàm xử lý riêng.
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

//vẽ button custom.
void WindowManager::HandleDrawItem(LPDRAWITEMSTRUCT dis) {
	if (uiManager_ && dis) {    // kiểm tra UIManager và DRAWITEMSTRUCT không null
		uiManager_->HandleOwnerDraw(dis);   // chuyển yêu cầu vẽ đến UIManager
    }
}

//Vị trí gọi: WM_APP_* messages. Nhận log message từ AppController thread và cập nhật UI log.
void WindowManager::HandleAppLog(LogMessage* msg) {
	if (uiManager_ && msg) {    // kiểm tra UIManager và LogMessage không null
		uiManager_->AddMessage(msg->text);  // thêm message vào log UI
    }
}

//Vị trí gọi: WM_APP_PRINTER_UPDATE. Cập nhật trạng thái máy in trên UI.
void WindowManager::HandlePrinterUpdate(PrinterStateMessage* msg) {
    if (uiManager_ && msg) {
		uiManager_->UpdatePrinterStatus(msg->statusText);   // Cập nhật text trạng thái máy in
		uiManager_->UpdatePrinterUIState(msg->state);       // Cập nhật trạng thái UI dựa trên trạng thái máy in
        Logger::GetInstance().Write(L"HandlePrinterUpdate: status=" + std::to_wstring((int)msg->state.status));

    }
}

//Vị trí gọi: WM_APP_CONNECTION_UPDATE. Nhận thông tin connected/disconnected.
void WindowManager::HandleConnectionUpdate(ConnectionMessage* msg) {
    if (uiManager_ && msg) {
        uiManager_->SetToggleState(msg->connected);     // Cập nhật trạng thái toggle
        
        // Thêm message kết nối/ngắt kết nối vào log UI
        if (msg->connected) { 
            uiManager_->AddMessage(L"✅ Đã kết nối đến " + msg->ipAddress);
        }
        else {
            uiManager_->AddMessage(L"🔌 Đã ngắt kết nối");
        }
    }
}

//Vị trí gọi: WM_APP_BUTTON_STATE. Cập nhật trạng thái button (active, disabled, running…)
void WindowManager::HandleButtonState(ButtonStateMessage* msg) {
    if (uiManager_ && msg) {
		uiManager_->UpdateButtonStates(msg->state);     // Cập nhật trạng thái button dựa trên trạng thái máy in
    }
}

//Vị trí gọi: WM_DESTROY. Xử lý dọn dẹp khi cửa sổ bị đóng.
//đảm bảo cleanup chỉ chạy 1 lần, threads stop trước khi quit.
void WindowManager::HandleDestroy() {
    /*
    Logger::GetInstance().Write(L"Main window destruction - starting cleanup");

	static bool cleanupDone = false;    // đảm bảo cleanup chỉ chạy 1 lần
	if (!cleanupDone) {                 // nếu chưa cleanup
		cleanupDone = true;             // đánh dấu đã cleanup

		if (appController_) {           // nếu AppController tồn tại
			appController_->ComprehensiveCleanup();// gọi comprehensive cleanup
        }
    }

	PostQuitMessage(0);     // gửi message thoát ứng dụng
    */
    Logger::GetInstance().Write(L"Main window destruction - starting cleanup");

    static std::atomic<bool> destroyHandled{ false };
    if (destroyHandled.exchange(true)) {
        return;
    }

    // ✅ DỪNG CONTROLLER TRƯỚC KHI POST QUIT
    if (appController_) {
        appController_->StopWorkerThread(1000);
    }

    PostQuitMessage(0);
}

//vị trí gọi: khi toggle kết nối được click.
//xử lý event người dùng → gọi AppController + update UI.
/*
void WindowManager::OnToggleClicked() {
    if (!appController_ || !uiManager_) return;     // kiểm tra AppController và UIManager tồn tại

    PrinterState currentState = appController_->GetCurrentState();  // lấy trạng thái máy in hiện tại
    // nếu đang disconnected → kết nối
    if (currentState.status == PrinterStatus::Disconnected) {
        std::wstring ip = uiManager_->GetIPAddress();   // lấy địa chỉ IP từ UI
        if (uiManager_->ValidateInput()) {      // kiểm tra địa chỉ IP hợp lệ

            uiManager_->SetToggleState(true);

            appController_->Connect(ip);        // gọi kết nối trong AppController
            uiManager_->AddMessage(L"🔄 Đang kết nối đến " + ip);
        }
        else {
            uiManager_->AddMessage(L"❌ Địa chỉ IP không hợp lệ");
            uiManager_->SetToggleState(false); // Reset toggle về off
        }
    }
    else {

        uiManager_->SetToggleState(false);


        appController_->Disconnect(); // gọi ngắt kết nối trong AppController
        uiManager_->AddMessage(L"🔌 Đang ngắt kết nối...");
    }
}

*/
void WindowManager::OnToggleClicked() {
    if (!appController_ || !uiManager_) return;

    // ✅ DÙNG TRẠNG THÁI TOGGLE HIỆN TẠI THAY VÌ TRẠNG THÁI MÁY IN
    bool isToggleCurrentlyOn = uiManager_->IsToggleOn();

    if (!isToggleCurrentlyOn) {
        // TOGGLE ĐANG OFF → USER CLICK ĐỂ BẬT (KẾT NỐI)
        std::wstring ip = uiManager_->GetIPAddress();
        if (uiManager_->ValidateInput()) {
            uiManager_->SetToggleState(true); // CẬP NHẬT SANG ON

            appController_->Connect(ip);
            uiManager_->AddMessage(L"🔄 Đang kết nối đến " + ip);
        }
        else {
            uiManager_->AddMessage(L"❌ Địa chỉ IP không hợp lệ");
            // KHÔNG CẬP NHẬT TOGGLE (GIỮ NGUYÊN OFF)
        }
    }
    else {
        // TOGGLE ĐANG ON → USER CLICK ĐỂ TẮT (NGẮT KẾT NỐI)
        uiManager_->SetToggleState(false); // CẬP NHẬT SANG OFF

        appController_->Disconnect();
        uiManager_->AddMessage(L"🔌 Đang ngắt kết nối...");
    }
}



//vị trí gọi: khi nút upload được click.
void WindowManager::OnUploadClicked() {
	if (!appController_ || !uiManager_) return;  // kiểm tra AppController và UIManager tồn tại

	std::wstring content = uiManager_->GetInputText();  // lấy nội dung từ UI
	if (!content.empty()) {     // nếu có nội dung
		//appController_->UploadContent(content);  // gọi upload nội dung trong AppController
        uiManager_->AddMessage(L"📤 Đã tải lên nội dung: " + content);
    }
    else {
        uiManager_->AddMessage(L"⚠️ Chưa có nội dung để tải lên");
    }
}

//vị trí gọi: khi nút start được click.
void WindowManager::OnStartClicked() {
	if (!appController_) return;    // kiểm tra AppController tồn tại

	appController_->StartJet(); //gọi StartJet trong AppController
    uiManager_->AddMessage(L"🚀 Khởi động jet...");
}

//vị trí gọi: khi nút print được click.
void WindowManager::OnPrintClicked() {
    if (!appController_ || !uiManager_) return;

	std::wstring content = uiManager_->GetInputText();      // Lấy nội dung in từ UI
	int count = uiManager_->GetCountValue();                // Lấy số lượng in từ UI

    if (!content.empty()) {
		appController_->StartPrinting(content, count);  //gọi StartPrinting trong AppController
        uiManager_->AddMessage(L"🖨️ Bắt đầu in...");
    }
    else {
        uiManager_->AddMessage(L"⚠️ Chưa có nội dung để in");
    }
}

//vị trí gọi: khi nút stop được click.
void WindowManager::OnStopClicked() {
	if (!appController_) return;    // kiểm tra AppController tồn tại

	appController_->StopPrinting(); //gọi StopPrinting trong AppController
    uiManager_->AddMessage(L"⏹️ Dừng in...");
}

//vị trí gọi: khi nút clear được click.
void WindowManager::OnClearClicked() {
	if (uiManager_) {   // kiểm tra UIManager tồn tại
		uiManager_->ClearMessages();    // xóa tất cả message trong log UI
        uiManager_->AddMessage(L"Đã xóa nhật ký");
    }
}

//vị trí gọi: khi nút set được click.
void WindowManager::OnSetClicked() {
	if (!appController_ || !uiManager_) return; // kiểm tra AppController và UIManager tồn tại

	int count = uiManager_->GetCountValue();    // lấy số lượng in từ UI
    if (count > 0) {
		appController_->SetCount(count); // gọi SetCount trong AppController
        uiManager_->AddMessage(L"Đã đặt số lượng in: " + std::to_wstring(count));
    }
    else {
        uiManager_->AddMessage(L"❌ Số lượng phải lớn hơn 0");
    }
}