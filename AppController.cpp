#include "AppController.h"
#include "Logger.h"
#include "FontManager.h"
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <future> 

AppController::AppController(HWND mainWindow)
    : mainWindow_(mainWindow),
	resourceTracker("AppController") {  // Khởi tạo ResourceTracker với tên "AppController"

	//==Khởi tạo các components chính==
	printerModel_ = std::make_unique<PrinterModel>();   // Model lưu trạng thái máy in
	rciClient_ = std::make_unique<RciClient>();     // Client RCI để giao tiếp với máy in

	//== ĐĂNG KÝ CALLBACK LOG TỪ RCI CLIENT ==
    rciClient_->SetMessageCallback([this](const std::wstring& msg, int level) { 
		this->SendLogMessage(L"[Máy in] " + msg, level);        // Gửi log message lên UI qua AppController
        });

    // ========== ĐĂNG KÝ CLEANUP TASKS ==========

	// 1. RCI Client cleanup
    resourceTracker.addCleanup("RciClient_Disconnect", [this]() {
        if (rciClient_) {
            Logger::GetInstance().Write(L"Disconnecting RCI client...");
            rciClient_->Disconnect();
            Logger::GetInstance().Write(L"RCI client disconnected");
        }
        });

    // 2. Worker thread cleanup
    resourceTracker.addCleanup("WorkerThread_Stop", [this]() {
        Logger::GetInstance().Write(L"Stopping worker thread...");
        StopWorkerThread(3000); // ✅ SỬA: Thêm timeout
        Logger::GetInstance().Write(L"Worker thread stopped");
        });

    // 3. Request queue cleanup
    resourceTracker.addCleanup("RequestQueue_Clear", [this]() {
        Logger::GetInstance().Write(L"Clearing request queue...");
        // Có thể thêm logic clear queue nếu cần
        Logger::GetInstance().Write(L"Request queue cleared");
        });

    // 4. Model cleanup (nếu cần)
    resourceTracker.addCleanup("PrinterModel_Cleanup", [this]() {
        if (printerModel_) {
            Logger::GetInstance().Write(L"Cleaning printer model...");
            printerModel_.reset();
            Logger::GetInstance().Write(L"Printer model cleaned");
        }
        });

    // 5. Message callback cleanup
    resourceTracker.addCleanup("MessageCallback_Clear", [this]() {
        if (rciClient_) {
            Logger::GetInstance().Write(L"Clearing message callbacks...");
            rciClient_->SetMessageCallback(nullptr);
            Logger::GetInstance().Write(L"Message callbacks cleared");
        }
        });

    Logger::GetInstance().Write(L"AppController initialized with " +
        std::to_wstring(resourceTracker.getPendingCleanupCount()) +
        L" cleanup tasks");
}

AppController::~AppController() {
    Logger::GetInstance().Write(L"AppController destructor called");

	// ✅ THÊM PROTECTION
    static std::atomic<bool> destructorCalled{ false };
    if (destructorCalled.exchange(true)) {
        Logger::GetInstance().Write(L"AppController destructor already called - skipping");
        return;
    }

    try {
        // ✅ DỪNG THREAD TRƯỚC KHI CLEANUP
        StopWorkerThread(2000);

        // ✅ CLEANUP RESOURCES
        resourceTracker.cleanupAll();
    }
    catch (const std::exception& e) {
        Logger::GetInstance().Write(L"Exception in destructor: " +
            std::wstring(e.what(), e.what() + strlen(e.what())), 2);
    }
}

// Khởi động worker thread
void AppController::StartWorkerThread() {
    if (running_) return;

    running_ = true;
	workerThread_ = std::thread(&AppController::WorkerLoop, this);  // Bắt đầu vòng lặp worker thread
	Logger::GetInstance().Write(L"Worker thread started");  // Log khởi động thread
}

// Dừng worker thread với timeout
bool AppController::StopWorkerThread(int timeoutMs) {
    running_ = false;
    bool success = true;

    // BIẾN THEO DÕI TRẠNG THÁI TOÀN CỤC
    static std::atomic<bool> stopInProgress{ false };

	// TRÁNH MULTIPLE CALLS
    if (stopInProgress.exchange(true)) {
        Logger::GetInstance().Write(L"StopWorkerThread already in progress - skipping");
        return false;
    }

    // SCOPE GUARD ĐẢM BẢO RESET
    struct ScopeGuard {
		std::atomic<bool>& flag;    // Tham chiếu đến biến cờ
        ~ScopeGuard() {
            // ĐẢM BẢO LUÔN RESET, NGAY CẢ KHI CÓ EXCEPTION
            flag.store(false, std::memory_order_release);
        }
    } guard{ stopInProgress };

    try {
		if (!workerThread_.joinable()) {    // Kiểm tra nếu thread đã dừng
            Logger::GetInstance().Write(L"Worker thread already stopped");
            return true;
        }

		if (timeoutMs <= 0) {       // Nếu không có timeout, join bình thường
            workerThread_.join();
            Logger::GetInstance().Write(L"Worker thread stopped gracefully");
        }
        else {
            // DÙNG FUTURE VỚI EXCEPTION HANDLING
            std::future<void> future;
            try {
				future = std::async(std::launch::async, [this]() {  // Bắt đầu async task để join thread
					try {      // Thử join thread
                        if (workerThread_.joinable()) {
                            workerThread_.join();
                        }
                    }
                    catch (const std::exception& e) {
                        Logger::GetInstance().Write(L"Join failed in async: " +
                            std::wstring(e.what(), e.what() + strlen(e.what())), 2);
                    }
                    });
			}   // BẮT LỖI TẠO ASYNC
            catch (const std::exception& e) {
                Logger::GetInstance().Write(L"Failed to create async task: " +
                    std::wstring(e.what(), e.what() + strlen(e.what())), 2);
                return false;
            }

			auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));// Chờ với timeout
			if (status == std::future_status::timeout) {    // Nếu timeout xảy ra
                Logger::GetInstance().Write(L"Worker thread stop timeout - emergency detach", 2);
                success = false;

                // EMERGENCY DETACH AN TOÀN
				try {   // Thử detach thread
                    if (workerThread_.joinable()) { 
                        workerThread_.detach();
                        Logger::GetInstance().Write(L"Worker thread emergency detached");
                    }
                }
				catch (const std::exception& e) {   // BẮT LỖI DETACH
                    Logger::GetInstance().Write(L"Emergency detach failed: " +
                        std::wstring(e.what(), e.what() + strlen(e.what())), 2);
                }
            }
            else if (status == std::future_status::ready) {
                Logger::GetInstance().Write(L"Worker thread stopped with timeout");
            }
        }
    }
	catch (const std::system_error& e) {    // BẮT LỖI HỆ THỐNG
        Logger::GetInstance().Write(L"System error in StopWorkerThread: " +
            std::wstring(e.what(), e.what() + strlen(e.what())), 2);
        success = false;

        //FALLBACK: THỬ DETACH NẾU JOIN FAIL
        try {
            if (workerThread_.joinable()) { 
                workerThread_.detach();
                Logger::GetInstance().Write(L"Worker thread detached after join failure");
            }
        }
        catch (...) {
            Logger::GetInstance().Write(L"Final detach also failed", 2);
        }
    }
    catch (const std::exception& e) {
        Logger::GetInstance().Write(L"General error in StopWorkerThread: " +
            std::wstring(e.what(), e.what() + strlen(e.what())), 2);
        success = false;
    }

    return success;
}

// Emergency cleanup cho tình huống khẩn cấp
void AppController::EmergencyCleanup() {
    Logger::GetInstance().Write(L"⚠️ EMERGENCY CLEANUP INITIATED");

    // ✅ THÊM PROTECTION
    static std::atomic<bool> emergencyCleanupInProgress{ false };
    if (emergencyCleanupInProgress.exchange(true)) {
        Logger::GetInstance().Write(L"EmergencyCleanup already in progress");
        return;
    }

    running_ = false;

    // ✅ DETACH AN TOÀN HƠN
    try {
        if (workerThread_.joinable()) {
            workerThread_.detach();
            Logger::GetInstance().Write(L"Worker thread emergency detached");
        }
    }
    catch (const std::exception& e) {
        Logger::GetInstance().Write(L"Emergency detach failed: " +
            std::wstring(e.what(), e.what() + strlen(e.what())), 2);
    }

    if (rciClient_) {
        rciClient_->Disconnect();
    }

    resourceTracker.clearWithoutCleanup();
    Logger::GetInstance().Write(L"⚠️ EMERGENCY CLEANUP COMPLETED");
}

void AppController::ComprehensiveCleanup() {
    Logger::GetInstance().Write(L"=== STARTING COMPREHENSIVE CLEANUP ===");

    // ✅ KIỂM TRA TRÁNH MULTIPLE CALLS
    static std::atomic<bool> comprehensiveCleanupInProgress{ false };
    if (comprehensiveCleanupInProgress.exchange(true)) {
        Logger::GetInstance().Write(L"ComprehensiveCleanup already in progress");
        return;
    }

    struct ScopeGuard {
        std::atomic<bool>& flag;
        ~ScopeGuard() { flag = false; }
    } guard{ comprehensiveCleanupInProgress };

    // 1. Stop và join tất cả threads với timeout
    Logger::GetInstance().Write(L"1. Stopping worker threads...");
    if (!StopWorkerThread(5000)) {
        Logger::GetInstance().Write(L"⚠️ Worker thread stop timeout - emergency mode", 1);
    }

    // 2. Đóng tất cả network connections
    Logger::GetInstance().Write(L"2. Closing network connections...");
    if (rciClient_) {
        rciClient_->Disconnect();
    }

    // 3. Release all GDI resources
    Logger::GetInstance().Write(L"3. Releasing GDI resources...");
    FontManager::GetInstance().Cleanup();

    // 4. Clear all containers và buffers
    Logger::GetInstance().Write(L"4. Clearing containers...");
    // Có thể thêm logic clear queue nếu cần

    // 5. Additional resource cleanup
    Logger::GetInstance().Write(L"5. Additional resource cleanup...");

    // Reset smart pointers
    printerModel_.reset();
    rciClient_.reset();

    // Clear main window reference
    mainWindow_ = nullptr;

    // Final resource tracker cleanup
    resourceTracker.cleanupAll();

    Logger::GetInstance().Write(L"=== COMPREHENSIVE CLEANUP COMPLETED ===");
}

// ================== UI PUBLIC API ==================

void AppController::Connect(const std::wstring& ipAddress) {
    Request req{ RequestType::RequestConnect };
    req.data = ipAddress;
    requestQueue_.Push(req);
}

void AppController::Disconnect() {
    Request req{ RequestType::RequestDisconnect };
    requestQueue_.Push(req);
}

void AppController::StartPrinting(const std::wstring& content, int count) {
    // Validation toàn diện
    if (!ValidatePrintContent(content)) {
        SendLogMessage(L"Nội dung in không hợp lệ", 2);
        return;
    }

    if (!ValidatePrintCount(count)) {
        SendLogMessage(L"Số lượng in phải từ 1-1000", 2);
        return;
    }

    Request req{ RequestType::RequestStartPrint };
    req.data = content;
    req.count = count;
    requestQueue_.Push(req);
}

void AppController::StopPrinting() {
    Request req{ RequestType::RequestStopPrint };
    requestQueue_.Push(req);
}

void AppController::SetCount(int count) {
    Request req{ RequestType::RequestSetCount };
    req.count = count;
    requestQueue_.Push(req);
}

void AppController::StartJet() {
    Request req{ RequestType::RequestStartJet };
    requestQueue_.Push(req);
}

void AppController::StopJet() {
    Request req{ RequestType::RequestStopJet };
    requestQueue_.Push(req);
}

bool AppController::ValidatePrintContent(const std::wstring& content) {
    return !content.empty() && content.length() <= 1000;
}

bool AppController::ValidatePrintCount(int count) {
    return count > 0 && count <= 1000;
}

// ================== WORKER THREAD LOOP ==================

void AppController::WorkerLoop() {
    const auto POLL_INTERVAL = std::chrono::milliseconds(500);
    const auto RECONNECT_INTERVAL = std::chrono::seconds(2);

    try {
        while (running_) {
            try {
                // 1. Kiểm tra kết nối và reconnect nếu cần
                if (!rciClient_ || !rciClient_->IsConnected()) {
                    if (autoReconnect_ && reconnectAttempts_ < MAX_RECONNECT_ATTEMPTS) {
                        TryReconnect();
                        std::this_thread::sleep_for(RECONNECT_INTERVAL);
                    }
                    else {
                        std::this_thread::sleep_for(POLL_INTERVAL);
                    }
                    continue;
                }

                // 2. Xử lý request từ queue (ưu tiên cao)
                Request req;
                if (requestQueue_.Pop(req, 50)) {
                    HandleRequest(req);
                }
                // 3. Nếu không có request, thực hiện periodic poll
                else {
                    DoPeriodicPoll();
                    std::this_thread::sleep_for(POLL_INTERVAL);
                }
            }
            catch (const std::exception& e) {
                // ✅ BẮT LỖI TRONG LOOP, KHÔNG THOÁT THREAD
                std::string errorMsg = e.what();
                SendLogMessage(L"Lỗi trong WorkerLoop: " +
                    std::wstring(errorMsg.begin(), errorMsg.end()), 2);
                std::this_thread::sleep_for(POLL_INTERVAL);
            }
            catch (...) {
                // ✅ BẮT MỌI LỖI KHÁC
                SendLogMessage(L"Lỗi không xác định trong WorkerLoop", 2);
                std::this_thread::sleep_for(POLL_INTERVAL);
            }
        }
    }
    catch (...) {
        // ✅ BẮT LỖI Ở CẤP ĐỘ CAO NHẤT - ĐẢM BẢO THREAD KHÔNG CRASH
        Logger::GetInstance().Write(L"CRITICAL: WorkerLoop bị crash", 2);
    }

    Logger::GetInstance().Write(L"WorkerLoop exited normally");
}

void AppController::HandleRequest(const Request& request) {
    try {
        switch (request.type) {
        case RequestType::RequestConnect:
            HandleConnectRequest(request);
            break;
        case RequestType::RequestDisconnect:
            HandleDisconnectRequest();
            break;
        case RequestType::RequestStartPrint:
            HandleStartPrintRequest(request);
            break;
        case RequestType::RequestStopPrint:
            HandleStopPrintRequest(); // ✅ ĐÃ SỬA
            break;
        case RequestType::RequestSetCount:
            HandleSetCountRequest(request);
            break;
        case RequestType::RequestStartJet:
            HandleStartJetRequest();
            break;
        case RequestType::RequestStopJet:
            HandleStopJetRequest();
            break;
        }
        SendStateUpdate();
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SendLogMessage(L"Lỗi xử lý request: " + std::wstring(errorMsg.begin(), errorMsg.end()), 2);
    }
}

void AppController::HandleDisconnectRequest() {
    if (rciClient_) {
        rciClient_->Disconnect();
    }

    PrinterState disconnectedState;
    disconnectedState.status = PrinterStatus::Disconnected;
    disconnectedState.statusText = L"Ngắt kết nối";
    disconnectedState.jetOn = false;
    disconnectedState.printing = false;
    printerModel_->SetState(disconnectedState);

    SendConnectionUpdate(false);
}

void AppController::DoPeriodicPoll() {
    // A) Luôn gửi request status định kỳ
    HandleStatusRequest();

    // B) Nếu đang in, lấy thêm print count
    if (ShouldGetPrintCount()) {
        HandlePrintCountRequest();
    }

    // C) Auto-start jet nếu cần (theo logic state machine)
    if (ShouldAutoStartJet()) {
        HandleStartJetRequest();
    }

    // Cập nhật state machine
    UpdatePrinterState();
}

void AppController::HandleStatusRequest() {
    std::wstring status;
    if (rciClient_ && rciClient_->GetStatusEx(status)) {
        printerModel_->SetStatusText(status);
    }
}

void AppController::HandlePrintCountRequest() {
    int currentCount = printerModel_->GetCurrentCount() + 1;
    printerModel_->UpdateJobProgress(currentCount);

    int targetCount = printerModel_->GetTargetCount();
    if (targetCount > 0 && currentCount >= targetCount) {
        HandleStopPrintRequest(); // ✅ SỬA: Dùng StopPrint thay vì StopJet
    }
}

void AppController::HandleStartPrintRequest(const Request& request) {
    if (!rciClient_ || !rciClient_->IsConnected()) {
        SendLogMessage(L"Lỗi: Chưa kết nối đến máy in", 2);
        return;
    }

    // 1. Start jet trước
    if (HandleStartJetRequest()) {
        // 2. Gửi nội dung in
        if (rciClient_->PrintText(request.data, request.count)) {
            printerModel_->SetCurrentJob(request.data, request.count);

            PrinterState printingState;
            printingState.status = PrinterStatus::Printing;
            printingState.statusText = L"Đang in";
            printingState.printing = true;
            printingState.jetOn = true;
            printerModel_->SetState(printingState);

            SendLogMessage(L"Đã bắt đầu in: " + request.data);
        }
    }
}

void AppController::HandleStopPrintRequest() {
    if (rciClient_ && rciClient_->IsConnected()) {
        rciClient_->StopJet();
    }

    PrinterState idleState;
    idleState.status = PrinterStatus::Idle;
    idleState.statusText = L"Đã dừng";
    idleState.printing = false;
    idleState.jetOn = false;
    printerModel_->SetState(idleState);

    SendLogMessage(L"Đã dừng in");
}

bool AppController::HandleStartJetRequest() {
    if (!rciClient_ || !rciClient_->IsConnected()) {
        SendLogMessage(L"Lỗi: Không thể khởi động jet - chưa kết nối", 2);
        return false;
    }

    if (rciClient_->StartJet()) {
        SendLogMessage(L"Đã khởi động jet");

        // Update printer state
        PrinterState state = printerModel_->GetState();
        state.jetOn = true;
        printerModel_->SetState(state);

        return true;
    }
    else {
        SendLogMessage(L"Lỗi: Không thể khởi động jet", 2);
        return false;
    }
}

void AppController::HandleStopJetRequest() {
    if (rciClient_ && rciClient_->StopJet()) {
        PrinterState idleState;
        idleState.status = PrinterStatus::Idle;
        idleState.statusText = L"Sẵn sàng";
        idleState.printing = false;
        idleState.jetOn = false;
        printerModel_->SetState(idleState);

        SendLogMessage(L"Đã dừng jet");
    }
}

void AppController::HandleConnectRequest(const Request& request) {
    // 1️⃣ Cập nhật UI ngay
    PrinterState connectingState;
    connectingState.status = PrinterStatus::Connecting;
    connectingState.statusText = L"Đang kết nối...";
    printerModel_->SetState(connectingState);

    SendStateUpdate();
    SendConnectionUpdate(true); // toggle bật ngay
    SendLogMessage(L"Đang kết nối đến " + request.data + L"...");

    // 2️⃣ Chạy kết nối trong thread riêng để không block UI
    std::thread([this, request]() {
        bool connectResult = false;
        if (rciClient_) {
            connectResult = rciClient_->Connect(request.data);
            if (connectResult) {
                printerModel_->SetConnectionInfo(request.data, 9100);
            }
        }

        // 3️⃣ Cập nhật state sau khi kết nối xong
        PrinterState finalState;
        if (connectResult) {
            finalState.status = PrinterStatus::Connected;
            finalState.statusText = L"Đã kết nối";
            reconnectAttempts_ = 0;
            SendLogMessage(L"✅ Kết nối thành công");
        }
        else {
            finalState.status = PrinterStatus::Error;
            finalState.statusText = L"Lỗi kết nối";
            reconnectAttempts_++;
            SendLogMessage(L"❌ Kết nối thất bại", 2);

        }

        printerModel_->SetState(finalState);
        SendStateUpdate();
        SendConnectionUpdate(connectResult);
        }).detach(); // detach để chạy nền
}

void AppController::HandleSetCountRequest(const Request& request) {
    printerModel_->SetCurrentJob(L"", request.count);
    SendLogMessage(L"Đã đặt số lượng in: " + std::to_wstring(request.count));
}

void AppController::TryReconnect() {
    if (reconnectAttempts_ >= MAX_RECONNECT_ATTEMPTS) {
        SendLogMessage(L"Đã vượt quá số lần reconnect tối đa", 2);
        return;
    }

    auto lastIp = printerModel_->GetIpAddress();
    if (!lastIp.empty()) {
        SendLogMessage(L"Tự động reconnect lần " +
            std::to_wstring(reconnectAttempts_ + 1) + L"...");
        Request req{ RequestType::RequestConnect };
        req.data = lastIp;
        HandleConnectRequest(req);
    }
}

// ================== STATE MACHINE LOGIC ==================

bool AppController::ShouldPollStatus() const {
    return rciClient_ && rciClient_->IsConnected();
}

bool AppController::ShouldGetPrintCount() const {
    auto state = printerModel_->GetState();
    return state.status == PrinterStatus::Printing;
}

bool AppController::ShouldAutoStartJet() const {
    auto state = printerModel_->GetState();
    return (state.status == PrinterStatus::Connected || state.status == PrinterStatus::Idle) &&
        printerModel_->HasPendingPrintJob();
}

void AppController::UpdatePrinterState() {
    auto currentState = printerModel_->GetState();

    if (!rciClient_ || !rciClient_->IsConnected()) {
        if (currentState.status != PrinterStatus::Disconnected &&
            currentState.status != PrinterStatus::Connecting) {

            PrinterState disconnectedState;
            disconnectedState.status = PrinterStatus::Disconnected;
            disconnectedState.statusText = L"Mất kết nối";
            disconnectedState.jetOn = false;
            disconnectedState.printing = false;
            printerModel_->SetState(disconnectedState);
        }
    }
}

// ================== THREAD-SAFE UI UPDATES ==================

void AppController::SendStateUpdate() {
    if (!mainWindow_) return;

    auto state = printerModel_->GetState();
    auto statusText = printerModel_->GetStatusText();
    auto* msg = new PrinterStateMessage{ state, statusText, L"" };

    if (!PostMessage(mainWindow_, WM_APP_PRINTER_UPDATE, (WPARAM)msg, 0)) {
        delete msg;
        Logger::GetInstance().Write(L"PostMessage failed for state update", 2);
    }
}

void AppController::SendLogMessage(const std::wstring& text, int level) {
    if (!mainWindow_) return;

    auto* msg = new LogMessage{ text, level };
    if (!PostMessage(mainWindow_, WM_APP_LOG, (WPARAM)msg, 0)) {
        delete msg; // Cleanup nếu PostMessage fail
    }
}

void AppController::SendConnectionUpdate(bool connected) {
    if (!mainWindow_) return;

    auto ip = printerModel_->GetIpAddress();
    auto* msg = new ConnectionMessage{ connected, ip, 9100 };
    if (!PostMessage(mainWindow_, WM_APP_CONNECTION_UPDATE, (WPARAM)msg, 0)) {
        delete msg; // Cleanup nếu PostMessage fail
    }
}

PrinterState AppController::GetCurrentState() const {
    return printerModel_->GetState();
}

bool AppController::IsConnected() const {
    return rciClient_ && rciClient_->IsConnected();
}