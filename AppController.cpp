
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

// ================== Helper functions (namespace ẩn) ==================
namespace {

    // T: kiểu status trả về từ RciClient::RequestStatusEx()
    template<typename T>
    PrinterStatus ConvertFromRciStatus(const T& raw) {
        // Có lỗi → Error
        if (raw.errorMask != 0) {
            return PrinterStatus::Error;
        }
        // Đang in
        if (raw.printing) {
            return PrinterStatus::Printing;
        }
        // Paused (đang tạm dừng)
        if (raw.paused) {
            return PrinterStatus::Idle;
        }
        // Chưa bật jet
        if (!raw.jetOn) {
            return PrinterStatus::Idle;
        }
        // Jet đang chạy, không in
        return PrinterStatus::Connected;
    }
    template<typename T>
    std::wstring BuildStatusText(const T& raw) {
        std::wstring text = L"Jet=" + std::to_wstring(raw.jetState) +
            L", Print=" + std::to_wstring(raw.printState) +
            L", ErrMask=0x" + std::to_wstring(raw.errorMask);
        return text;
    }
} // namespace
// ================== Constructor / Destructor ==================
AppController::AppController(HWND mainWindow)
    : mainWindow_(mainWindow),
    resourceTracker("AppController") {

    //== Khởi tạo các components chính ==
    printerModel_ = std::make_unique<PrinterModel>();   // Model lưu trạng thái máy in
    rciClient_ = std::make_unique<RciClient>();      // Client RCI Linx 8900

    //== ĐĂNG KÝ CALLBACK LOG TỪ RCI CLIENT ==
    // Giả định RciClient cũ có SetMessageCallback giống bản mới
    rciClient_->SetMessageCallback([this](const std::wstring& msg, int level) {
        this->SendLogMessage(L"[Máy in] " + msg, level);
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
        StopWorkerThread(3000);
        Logger::GetInstance().Write(L"Worker thread stopped");
        });

    // 3. Request queue cleanup
    resourceTracker.addCleanup("RequestQueue_Clear", [this]() {
        Logger::GetInstance().Write(L"Clearing request queue...");
        // Có thể thêm logic clear queue nếu cần
        Logger::GetInstance().Write(L"Request queue cleared");
        });

    // 4. Model cleanup
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

    Logger::GetInstance().Write(
        L"AppController initialized with " +
        std::to_wstring(resourceTracker.getPendingCleanupCount()) +
        L" cleanup tasks"
    );
}

AppController::~AppController() {
    Logger::GetInstance().Write(L"AppController destructor called");

    static std::atomic<bool> destructorCalled{ false };
    if (destructorCalled.exchange(true)) {
        Logger::GetInstance().Write(L"AppController destructor already called - skipping");
        return;
    }

    try {
        StopWorkerThread(2000);
        resourceTracker.cleanupAll();
    }
    catch (const std::exception& e) {
        Logger::GetInstance().Write(
            L"Exception in destructor: " +
            std::wstring(e.what(), e.what() + strlen(e.what())), 2
        );
    }
}

// ================== Worker Thread Control ==================

void AppController::StartWorkerThread() {
    if (running_) return;

    running_ = true;
    workerThread_ = std::thread(&AppController::WorkerLoop, this);
    Logger::GetInstance().Write(L"Worker thread started");
}

bool AppController::StopWorkerThread(int timeoutMs) {
    running_ = false;
    bool success = true;

    static std::atomic<bool> stopInProgress{ false };
    if (stopInProgress.exchange(true)) {
        Logger::GetInstance().Write(L"StopWorkerThread already in progress - skipping");
        return false;
    }

    struct ScopeGuard {
        std::atomic<bool>& flag;
        ~ScopeGuard() { flag.store(false, std::memory_order_release); }
    } guard{ stopInProgress };

    try {
        if (!workerThread_.joinable()) {
            Logger::GetInstance().Write(L"Worker thread already stopped");
            return true;
        }

        if (timeoutMs <= 0) {
            workerThread_.join();
            Logger::GetInstance().Write(L"Worker thread stopped gracefully");
        }
        else {
            std::future<void> future;
            try {
                future = std::async(std::launch::async, [this]() {
                    try {
                        if (workerThread_.joinable()) {
                            workerThread_.join();
                        }
                    }
                    catch (const std::exception& e) {
                        Logger::GetInstance().Write(
                            L"Join failed in async: " +
                            std::wstring(e.what(), e.what() + strlen(e.what())), 2
                        );
                    }
                    });
            }
            catch (const std::exception& e) {
                Logger::GetInstance().Write(
                    L"Failed to create async task: " +
                    std::wstring(e.what(), e.what() + strlen(e.what())), 2
                );
                return false;
            }

            auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));
            if (status == std::future_status::timeout) {
                Logger::GetInstance().Write(L"Worker thread stop timeout - emergency detach", 2);
                success = false;

                try {
                    if (workerThread_.joinable()) {
                        workerThread_.detach();
                        Logger::GetInstance().Write(L"Worker thread emergency detached");
                    }
                }
                catch (const std::exception& e) {
                    Logger::GetInstance().Write(
                        L"Emergency detach failed: " +
                        std::wstring(e.what(), e.what() + strlen(e.what())), 2
                    );
                }
            }
            else if (status == std::future_status::ready) {
                Logger::GetInstance().Write(L"Worker thread stopped with timeout");
            }
        }
    }
    catch (const std::system_error& e) {
        Logger::GetInstance().Write(
            L"System error in StopWorkerThread: " +
            std::wstring(e.what(), e.what() + strlen(e.what())), 2
        );
        success = false;
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
        Logger::GetInstance().Write(
            L"General error in StopWorkerThread: " +
            std::wstring(e.what(), e.what() + strlen(e.what())), 2
        );
        success = false;
    }

    return success;
}

// ================== Emergency / Comprehensive Cleanup ==================

void AppController::EmergencyCleanup() {
    Logger::GetInstance().Write(L"⚠️ EMERGENCY CLEANUP INITIATED");

    static std::atomic<bool> emergencyCleanupInProgress{ false };
    if (emergencyCleanupInProgress.exchange(true)) {
        Logger::GetInstance().Write(L"EmergencyCleanup already in progress");
        return;
    }

    running_ = false;

    try {
        if (workerThread_.joinable()) {
            workerThread_.detach();
            Logger::GetInstance().Write(L"Worker thread emergency detached");
        }
    }
    catch (const std::exception& e) {
        Logger::GetInstance().Write(
            L"Emergency detach failed: " +
            std::wstring(e.what(), e.what() + strlen(e.what())), 2
        );
    }

    if (rciClient_) {
        rciClient_->Disconnect();
    }

    resourceTracker.clearWithoutCleanup();
    Logger::GetInstance().Write(L"⚠️ EMERGENCY CLEANUP COMPLETED");
}

void AppController::ComprehensiveCleanup() {
    Logger::GetInstance().Write(L"=== STARTING COMPREHENSIVE CLEANUP ===");

    static std::atomic<bool> comprehensiveCleanupInProgress{ false };
    if (comprehensiveCleanupInProgress.exchange(true)) {
        Logger::GetInstance().Write(L"ComprehensiveCleanup already in progress");
        return;
    }

    struct ScopeGuard {
        std::atomic<bool>& flag;
        ~ScopeGuard() { flag = false; }
    } guard{ comprehensiveCleanupInProgress };

    Logger::GetInstance().Write(L"1. Stopping worker threads...");
    if (!StopWorkerThread(5000)) {
        Logger::GetInstance().Write(L"⚠️ Worker thread stop timeout - emergency mode", 1);
    }

    Logger::GetInstance().Write(L"2. Closing network connections...");
    if (rciClient_) {
        rciClient_->Disconnect();
    }

    Logger::GetInstance().Write(L"3. Releasing GDI resources...");
    FontManager::GetInstance().Cleanup();

    Logger::GetInstance().Write(L"4. Clearing containers...");

    Logger::GetInstance().Write(L"5. Additional resource cleanup...");

    printerModel_.reset();
    rciClient_.reset();
    mainWindow_ = nullptr;

    resourceTracker.cleanupAll();

    Logger::GetInstance().Write(L"=== COMPREHENSIVE CLEANUP COMPLETED ===");
}

// ================== UI Public API ==================

void AppController::Connect(const std::wstring& ipAddress) {
    SendLogMessage(L"[DEBUG] RequestConnect pushed", 1);

    Request req{ RequestType::RequestConnect };
    req.data = ipAddress;
    requestQueue_.Push(req);
}

void AppController::Disconnect() {
    Request req{ RequestType::RequestDisconnect };
    requestQueue_.Push(req);
}

void AppController::StartPrinting(const std::wstring& content, int count) {
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

                //---------------------------------------------------------
                // 0) ƯU TIÊN TUYỆT ĐỐI: XỬ LÝ REQUEST TỪ UI TRƯỚC
                //---------------------------------------------------------
                Request req;
                if (requestQueue_.Pop(req, 50)) {
                    SendLogMessage(L"[DEBUG] WorkerLoop POP request: " + std::to_wstring((int)req.type), 1);
                    HandleRequest(req);
                    std::this_thread::sleep_for(POLL_INTERVAL);
                    continue;       // QUAN TRỌNG: KHÔNG CHO RECONNECT CHẠY KHI MỚI RECEIVE REQUEST
                }

                //---------------------------------------------------------
                // 1) KIỂM TRA KẾT NỐI
                //---------------------------------------------------------
                bool connected = (rciClient_ && rciClient_->IsConnected());
                PrinterState currentState = printerModel_->GetState();

                if (!connected) {

                    //-----------------------------------------------------
                    // 1.1) Nếu đang CONNECTING → KHÔNG reconnect
                    //-----------------------------------------------------
                    if (currentState.status == PrinterStateType::Connecting) {
                        std::this_thread::sleep_for(POLL_INTERVAL);
                        continue;
                    }

                    //-----------------------------------------------------
                    // 1.2) Nếu autoReconnect TẮT → không reconnect
                    //-----------------------------------------------------
                    if (!autoReconnect_) {
                        reconnectAttempts_ = 0;
                        std::this_thread::sleep_for(POLL_INTERVAL);
                        continue;
                    }

                    //-----------------------------------------------------
                    // 1.3) Không có IP → không reconnect
                    //-----------------------------------------------------
                    auto lastIp = printerModel_->GetIpAddress();
                    if (lastIp.empty()) {
                        static int logCounter = 0;
                        if (logCounter++ % 20 == 0)
                            SendLogMessage(L"⚠️ Không có IP để reconnect", 2);

                        std::this_thread::sleep_for(POLL_INTERVAL);
                        continue;
                    }

                    //-----------------------------------------------------
                    // 1.4) Giới hạn số lần reconnect
                    //-----------------------------------------------------
                    if (reconnectAttempts_ >= MAX_RECONNECT_ATTEMPTS) {
                        std::this_thread::sleep_for(POLL_INTERVAL);
                        continue;
                    }

                    //-----------------------------------------------------
                    // 1.5) Thực hiện reconnect
                    //-----------------------------------------------------
                    SendLogMessage(L"[DEBUG] WorkerLoop: Performing auto reconnect...", 1);
                    TryReconnect();
                    std::this_thread::sleep_for(RECONNECT_INTERVAL);
                    continue;
                }

                //---------------------------------------------------------
                // 2) Nếu đã kết nối → poll hoặc idle
                //---------------------------------------------------------
                DoPeriodicPoll();
                std::this_thread::sleep_for(POLL_INTERVAL);
            }

            //-------------------------------------------------------------
            // Catch lỗi trong vòng lặp nội bộ
            //-------------------------------------------------------------
            catch (const std::exception& e) {
                SendLogMessage(L"Lỗi trong WorkerLoop: " +
                    std::wstring(e.what(), e.what() + strlen(e.what())), 2);
                std::this_thread::sleep_for(POLL_INTERVAL);
            }
            catch (...) {
                SendLogMessage(L"Lỗi không xác định trong WorkerLoop", 2);
                std::this_thread::sleep_for(POLL_INTERVAL);
            }
        }
    }

    //-------------------------------------------------------------
    // Catch lỗi crash toàn bộ loop
    //-------------------------------------------------------------
    catch (...) {
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
            HandleStopPrintRequest();
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
        default:
            break;
        }

        SendStateUpdate();
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SendLogMessage(L"Lỗi xử lý request: " +
            std::wstring(errorMsg.begin(), errorMsg.end()), 2);
    }
}

// ================== REQUEST HANDLERS ==================

void AppController::HandleDisconnectRequest() {
    if (rciClient_) {
        rciClient_->Disconnect();
    }

    PrinterState disconnectedState;
    disconnectedState.status = PrinterStateType::Disconnected;
    disconnectedState.statusText = L"Ngắt kết nối";
    disconnectedState.jetOn = false;
    disconnectedState.printing = false;
    printerModel_->SetState(disconnectedState);

    SendConnectionUpdate(false);
}

// Poll định kỳ
void AppController::DoPeriodicPoll() {
    HandleStatusRequest();

    if (ShouldGetPrintCount()) {
        HandlePrintCountRequest();
    }

    if (ShouldAutoStartJet()) {
        HandleStartJetRequest();
    }

    UpdatePrinterState();
}

// Gửi lệnh STATUS 0x14 đến Linx và cập nhật model
void AppController::HandleStatusRequest() {
    if (!rciClient_ || !rciClient_->IsConnected())
        return;

    PrinterStatus raw = rciClient_->RequestStatusEx();

    // Text mô tả tình trạng
    std::wstring text =
        L"Jet=" + std::to_wstring(raw.jetState) +
        L" Print=" + std::to_wstring(raw.printState) +
        L" ErrMask=0x" + std::to_wstring(raw.errorMask);

    printerModel_->SetStatusText(text);

    PrinterState st = printerModel_->GetState(); // lấy state cũ

    // ----- Mapping FLAGS -----
    st.jetOn = raw.jetOn;
    st.printing = raw.printing;

    // ----- Mapping STATUS MACHINE -----
    if (raw.errorMask != 0) {
        st.status = PrinterStateType::Error;
        st.errorMessage = L"ErrorMask: 0x" + std::to_wstring(raw.errorMask);
    }
    else if (raw.printing) {
        st.status = PrinterStateType::Printing;
    }
    else if (raw.jetOn) {
        st.status = PrinterStateType::Connected;
    }
    else {
        st.status = PrinterStateType::Idle;
    }

    st.statusText = text;

    printerModel_->SetState(st);
}

// Tạm thời mô phỏng print count bằng counter trong model
void AppController::HandlePrintCountRequest() {
    int currentCount = printerModel_->GetCurrentCount() + 1;
    printerModel_->UpdateJobProgress(currentCount);

    int targetCount = printerModel_->GetTargetCount();
    if (targetCount > 0 && currentCount >= targetCount) {
        HandleStopPrintRequest();
    }
}

// Bắt đầu in: dùng LoadMessage + StartPrint của RCI thật
void AppController::HandleStartPrintRequest(const Request& req) {

    if (!rciClient_ || !rciClient_->IsConnected()) {
        SendLogMessage(L"Chưa kết nối máy in", 2);
        return;
    }

    // auto bật jet
    if (!HandleStartJetRequest())
        return;

    // chuẩn hóa tên message 8 ký tự
    std::wstring wname = req.data;
    if (wname.size() > 8) wname = wname.substr(0, 8);

    std::string name;
    int len = WideCharToMultiByte(CP_ACP, 0, wname.c_str(), -1, nullptr, 0, NULL, NULL);
    name.resize(len);
    WideCharToMultiByte(CP_ACP, 0, wname.c_str(), -1, &name[0], len, NULL, NULL);

    if (!rciClient_->LoadMessage(name, (uint16_t)req.count)) {
        SendLogMessage(L"Lỗi LoadMessage", 2);
        return;
    }

    if (!rciClient_->StartPrint()) {
        SendLogMessage(L"Lỗi StartPrint", 2);
        return;
    }

    printerModel_->SetCurrentJob(req.data, req.count);

    PrinterState st = printerModel_->GetState();
    st.printing = true;
    st.jetOn = true;
    st.status = PrinterStateType::Printing;
    st.statusText = L"Đang in";
    printerModel_->SetState(st);
}

void AppController::HandleStopPrintRequest() {
    if (rciClient_->IsConnected())
        rciClient_->StopPrint();

    auto st = printerModel_->GetState();
    st.printing = false;
    st.status = PrinterStateType::Idle;
    st.statusText = L"Dừng in";
    printerModel_->SetState(st);

    SendLogMessage(L"Đã dừng in");
}


bool AppController::HandleStartJetRequest() {
    if (!rciClient_ || !rciClient_->IsConnected()) return false;

    if (!rciClient_->StartJet()) {
        SendLogMessage(L"Không thể bật Jet", 2);
        return false;
    }

    auto st = printerModel_->GetState();
    st.jetOn = true;
    st.status = PrinterStateType::Connected;
    st.statusText = L"Jet đã bật";
    printerModel_->SetState(st);

    SendLogMessage(L"Jet đã bật");
    return true;
}

void AppController::HandleStopJetRequest() {
    if (rciClient_ && rciClient_->IsConnected()) {
        rciClient_->StopJet();
    }

    auto st = printerModel_->GetState();
    st.jetOn = false;
    st.printing = false;
    st.status = PrinterStateType::Idle;
    st.statusText = L"Jet đã dừng";
    printerModel_->SetState(st);

    SendLogMessage(L"Jet đã dừng");
}


void AppController::HandleConnectRequest(const Request& req) {
    DisableAutoReconnect();

    PrinterState s = printerModel_->GetState();
    s.status = PrinterStateType::Connecting;
    s.statusText = L"Đang kết nối...";
    printerModel_->SetState(s);
    SendStateUpdate();

    // Lưu IP trước khi connect
    printerModel_->SetConnectionInfo(req.data, 9100);

    SendLogMessage(L"[DEBUG] HandleConnectRequest ENTER", 1);
    SendLogMessage(L"🔌 Đang kết nối tới " + req.data + L":9100...", 1);

    bool ok = rciClient_->Connect(req.data, 9100, 3000);

    PrinterState st = printerModel_->GetState();

    if (ok) {
        reconnectAttempts_ = 0;
        st.status = PrinterStateType::Connected;
        st.statusText = L"Đã kết nối";
        SendLogMessage(L"✅ Đã kết nối", 1);
    }
    else {
        st.status = PrinterStateType::Error;
        st.statusText = L"Lỗi kết nối";
        SendLogMessage(L"❌ Không thể kết nối", 2);
    }

    printerModel_->SetState(st);
    SendStateUpdate();
    SendConnectionUpdate(ok);

    EnableAutoReconnect();
}

void AppController::HandleSetCountRequest(const Request& request) {
    printerModel_->SetCurrentJob(L"", request.count);
    SendLogMessage(L"Đã đặt số lượng in: " + std::to_wstring(request.count));
}

void AppController::TryReconnect() {
    if (reconnectAttempts_ >= MAX_RECONNECT_ATTEMPTS) {
        return; // không log nữa
    }

    auto lastIp = printerModel_->GetIpAddress();
    if (lastIp.empty()) {
        return; // không log nữa
    }

    reconnectAttempts_++;

    SendLogMessage(
        L"Tự động reconnect lần " + std::to_wstring(reconnectAttempts_) +
        L" tới " + lastIp + L"...", 1);

    Request req{ RequestType::RequestConnect };
    req.data = lastIp;
    requestQueue_.Push(req);
}

// ================== STATE MACHINE LOGIC ==================

bool AppController::ShouldPollStatus() const {
    return rciClient_ && rciClient_->IsConnected();
}

bool AppController::ShouldGetPrintCount() const {
    auto state = printerModel_->GetState();
    return state.status == PrinterStateType::Printing;
}

bool AppController::ShouldAutoStartJet() const {
    auto state = printerModel_->GetState();
    return (state.status == PrinterStateType::Connected ||
        state.status == PrinterStateType::Idle) &&
        printerModel_->HasPendingPrintJob();
}

void AppController::UpdatePrinterState() {
    auto currentState = printerModel_->GetState();

    if (!rciClient_ || !rciClient_->IsConnected()) {
        if (currentState.status != PrinterStateType::Disconnected &&
            currentState.status != PrinterStateType::Connecting) {

            PrinterState disconnectedState;
            disconnectedState.status = PrinterStateType::Disconnected;
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
        delete msg;
    }
}

void AppController::SendConnectionUpdate(bool connected) {
    if (!mainWindow_) return;

    auto ip = printerModel_->GetIpAddress();
    auto* msg = new ConnectionMessage{ connected, ip, 9100 };
    if (!PostMessage(mainWindow_, WM_APP_CONNECTION_UPDATE, (WPARAM)msg, 0)) {
        delete msg;
    }
}

PrinterState AppController::GetCurrentState() const {
    return printerModel_->GetState();
}

bool AppController::IsConnected() const {
    return rciClient_ && rciClient_->IsConnected();
}
void AppController::SetLastIp(const std::wstring& ip) {
    if (printerModel_) {
        printerModel_->SetConnectionInfo(ip, 9100);
    }
}
