
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
#include "PrintPersistData.h"
#include "PrintPersistStorage.h"
#include "MessageBuilder.h"
#include "EncodingUtils.h"
// ================== Helper functions (namespace ẩn) ==================
namespace {
    template<typename T>  // T: kiểu status trả về từ RciClient::RequestStatusEx()
    PrinterStatus ConvertFromRciStatus(const T& raw) { 
        if (raw.errorMask != 0) {   // Có lỗi → Error
            return PrinterStatus::Error;
        }
        if (raw.printing) {        // Đang in
            return PrinterStatus::Printing;
        }
        if (raw.paused) {        // Paused (đang tạm dừng)
            return PrinterStatus::Idle;
        }
        if (!raw.jetOn) {        // Chưa bật jet
            return PrinterStatus::Idle;
        }
        return PrinterStatus::Connected;        // Jet đang chạy, không in
    }
    template<typename T>
    std::wstring BuildStatusText(const T& raw) {
        std::wstring text = L"Jet=" + std::to_wstring(raw.jetState) +
            L", Print=" + std::to_wstring(raw.printState) +
            L", ErrMask=0x" + std::to_wstring(raw.errorMask);
        return text;
    }
}
// ================== Constructor / Destructor ==================
AppController::AppController(HWND mainWindow): mainWindow_(mainWindow),resourceTracker("AppController"), lastJetOn_(false) {
    printerModel_ = std::make_unique<PrinterModel>();   // Model lưu trạng thái máy in
    rciClient_ = std::make_unique<RciClient>();      // Client RCI Linx 8900

    //== ĐĂNG KÝ CALLBACK LOG TỪ RCI CLIENT ==
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
        std::to_wstring(resourceTracker.getPendingCleanupCount()) + L" cleanup tasks"
    );
}

AppController::~AppController() {
    static std::atomic<bool> destructorCalled{ false };
    if (destructorCalled.exchange(true)) {
        return;
    }
    if (printerModel_) {
        SavePrintDataOnExit();
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
}

bool AppController::StopWorkerThread(int timeoutMs) {
    running_ = false;
    bool success = true;

    static std::atomic<bool> stopInProgress{ false };
    if (stopInProgress.exchange(true)) {
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

void AppController::SavePrintDataOnExit(){
    PrintPersistData data;
    data.message = WideToUtf8(printerModel_->GetCurrentJobContent());
    data.targetCount = printerModel_->GetTargetCount();
    data.printedCount = printerModel_->GetCurrentCount();
    PrintPersistStorage::Save(data);
}

void AppController::LoadPrintDataOnStart(){
    if (persistLoaded_) return;
    persistLoaded_ = true;

    auto loaded = PrintPersistStorage::Load();
    if (!loaded) return;

    const auto& d = *loaded;

    printerModel_->SetCurrentJob(Utf8ToWide(d.message), d.targetCount);
    printerModel_->UpdateJobProgress(d.printedCount);
    SendStateUpdate();
}
// ================== Emergency / Comprehensive Cleanup ==================
void AppController::EmergencyCleanup() {
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
    if (printerModel_) {
        Logger::GetInstance().Write(L"[Persist] ComprehensiveCleanup -> SavePrintDataOnExit()");
        SavePrintDataOnExit();
    }
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
    // 1. set state Connecting để UI đổi sang nền vàng
    PrinterState st = printerModel_->GetState();
    st.status = PrinterStateType::Connecting;
    st.statusText = L"Đang kết nối...";
    printerModel_->SetState(st);
    printerModel_->SetStatusText(st.statusText);
    SendStateUpdate();

    // 2. push request connect

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

void AppController::UploadContent(const std::wstring& content, int count) {
    if (!ValidatePrintContent(content)) {
        SendLogMessage(L"Nội dung in không hợp lệ", 2);
        return;
    }
    if (count <= 0) count = printerModel_->GetTargetCount();
    Request req{ RequestType::RequestUploadContent };
    req.data = content;
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

void AppController::UploadRemoteFieldData(const std::wstring& fieldName,const std::wstring& value)
{
    Request req{ RequestType::RequestUploadRemoteField };
    req.fieldName = fieldName;
    req.data = value;              // tận dụng sẵn field data (wstring)
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
    const auto POLL_INTERVAL = std::chrono::milliseconds(100);
    const auto RECONNECT_INTERVAL = std::chrono::seconds(2);

    try {
        while (running_) {
            try {
                // 0) ƯU TIÊN TUYỆT ĐỐI: XỬ LÝ REQUEST TỪ UI TRƯỚC
                Request req;
                if (requestQueue_.Pop(req, 50)) {
                    HandleRequest(req);
                    continue;      
                }
                // 1) KIỂM TRA KẾT NỐI
                bool connected = (rciClient_ && rciClient_->IsConnected());
                PrinterState currentState = printerModel_->GetState();
                if (!connected) {
                    // 1.1) Nếu đang CONNECTING → KHÔNG reconnect
                    if (currentState.status == PrinterStateType::Connecting) {
                        std::this_thread::sleep_for(POLL_INTERVAL);
                        continue;
                    }
                    // 1.2) Nếu autoReconnect TẮT → không reconnect
                    if (!autoReconnect_) {
                        reconnectAttempts_ = 0;
                        std::this_thread::sleep_for(POLL_INTERVAL);
                        continue;
                    }
                    // 1.3) Không có IP → không reconnect
                    auto lastIp = printerModel_->GetIpAddress();
                    if (lastIp.empty()) {
                        static int logCounter = 0;
                        if (logCounter++ % 20 == 0)

                        std::this_thread::sleep_for(POLL_INTERVAL);
                        continue;
                    }
                    // 1.4) Giới hạn số lần reconnect
                    if (reconnectAttempts_ >= MAX_RECONNECT_ATTEMPTS) {
                        std::this_thread::sleep_for(POLL_INTERVAL);
                        continue;
                    }
                    // 1.5) Thực hiện reconnect
                    TryReconnect();
                    std::this_thread::sleep_for(RECONNECT_INTERVAL);
                    continue;
                }
                // 2) Nếu đã kết nối → poll hoặc idle
                DoPeriodicPoll();
                std::this_thread::sleep_for(POLL_INTERVAL);
            }
            // Catch lỗi trong vòng lặp nội bộ
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
    // Catch lỗi crash toàn bộ loop
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
        case RequestType::RequestUploadContent:
            HandleUploadContentRequest(request);
            break;
        case RequestType::RequestUploadRemoteField:
            HandleUploadRemoteFieldRequest(request);
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
    DisableAutoReconnect();

    jetTransitioning_ = false;   // ⬅️ RESET
    hasInitialStatus_ = false;   // ⬅️ RESET
    lastJetOn_ = false;          // ⬅️ RESET

    if (rciClient_) {
        rciClient_->Disconnect();
    }
    PrinterState st = printerModel_->GetState();

    st.status = PrinterStateType::Disconnected;
    st.statusText = L"Ngắt kết nối";
    st.jetOn = false;
    st.printing = false;

    printerModel_->SetState(st);

    SendStateUpdate();            // bắt buộc
    SendConnectionUpdate(false);  // UI chỉ là phụ
    SendLogMessage(L"Đã ngắt kết nối", 0);
}

void AppController::HandleUploadRemoteFieldRequest(const Request& req)
{
    if (!rciClient_ || !rciClient_->IsConnected()) {
        SendLogMessage(L"❌ Chưa kết nối máy in", 2);
        return;
    }
    std::string field = WideToUtf8(req.fieldName);
    std::string value = WideToUtf8(req.data);

    if (!rciClient_->SendRemoteFieldDataByName(field, value, 3000))   return;
    int target = printerModel_->GetTargetCount();
    if (target <= 0) target = 1;

    printerModel_->SetCurrentJob(req.data, target);
    SavePrintDataOnExit();
    SendLogMessage(L"✅ Đã gửi nội dung vào Remote Field (0x9E) và lưu nội dung", 1);
}

void AppController::DoPeriodicPoll() {
    if (!rciClient_ || !rciClient_->IsConnected()) return;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCommandTime_).count();

    if (elapsed < 300) return;
    HandleStatusRequest();
    if (ShouldGetPrintCount()) {
        HandlePrintCountRequest();
    }
    if (ShouldAutoStartJet()) {
        HandleStartJetRequest();
    }
    UpdatePrinterState();
}

void AppController::HandleStatusRequest() {
    if (!rciClient_ || !rciClient_->IsConnected()) return;
    PrinterStatus raw = rciClient_->RequestStatusEx();
    if (!rciClient_->IsConnected()) return;
    PrinterState st = printerModel_->GetState();
    std::wstring text = StateToText(st);
    printerModel_->SetStatusText(text);
    // ===== Mapping FLAGS =====
    st.jetOn = raw.jetOn;
    st.printing = raw.printing;
    st.jetTransitioning = jetTransitioning_;
    st.statusText = text;
    bool jetOnNow = raw.jetOn;
 
    if (jetTransitioning_) {// 🔒 ĐANG TRONG TRANSITION
        if (!hasInitialStatus_) {    // Lần đầu tiên sau khi bắt đầu transition
            lastJetOn_ = jetOnNow;
            hasInitialStatus_ = true;
        }
        else if (jetOnNow != lastJetOn_) {   // Jet đã đổi trạng thái → KẾT THÚC TRANSITION
            jetTransitioning_ = false;
            st.jetTransitioning = false;
            
            if (raw.errorMask != 0) {   // Chuyển sang trạng thái ổn định
                st.status = PrinterStateType::Error;
                st.errorMessage = L"ErrorMask: 0x" + std::to_wstring(raw.errorMask);
            }
            else if (raw.printing) st.status = PrinterStateType::Printing;
            else if (raw.jetOn) st.status = PrinterStateType::Ready;
            else st.status = PrinterStateType::Idle;
            lastJetOn_ = jetOnNow;
            printerModel_->SetState(st);
            SendStateUpdate();
            return;
        }
        else {
            Logger::GetInstance().Write(L"🔄 HandleStatusRequest: Jet chưa thay đổi, " L"vẫn đang chờ...", 1);
        }
        // Vẫn đang trong transition, chỉ cập nhật thông tin phụ
        printerModel_->SetState(st);
        SendStateUpdate();
        return;
    }
    // 🚦 KHÔNG CÓ TRANSITION - State machine bình thường
    if (raw.errorMask != 0) {
        st.status = PrinterStateType::Error;
        st.errorMessage = L"ErrorMask: 0x" + std::to_wstring(raw.errorMask);
    }
    else if (raw.printing) st.status = PrinterStateType::Printing;
    else if (raw.jetOn) st.status = PrinterStateType::Ready;
    else st.status = PrinterStateType::Idle;
    // Cập nhật tracking
    lastJetOn_ = jetOnNow;
    hasInitialStatus_ = true;
    printerModel_->SetState(st);
    SendStateUpdate();
}

void AppController::HandlePrintCountRequest() {
    if (!rciClient_ || !rciClient_->IsConnected()) return;
    uint32_t count = 0;
    std::string msgName;
    if (!rciClient_->RequestMessagePrintCount(count, msgName, "", 3000)) return;
    if (msgName.empty()) return;

    int cur = printerModel_->GetCurrentCount();
    if (count == 0 && cur > 0) return;
    printerModel_->UpdateJobProgress((int)count);

    int targetCount = printerModel_->GetTargetCount();
    if (targetCount > 0 && (int)count >= targetCount) {
        HandleStopPrintRequest();
    }
}

bool AppController::HandleStartJetRequest() {
    if (!rciClient_ || !rciClient_->IsConnected()) return false;
    if (jetTransitioning_) return false;

    jetTransitioning_ = true;
    hasInitialStatus_ = false; // QUAN TRỌNG: reset để theo dõi

    PrinterState st = printerModel_->GetState();
    st.status = PrinterStateType::StartingJet;
    st.statusText = L"Đang khởi động Jet...";
    st.jetTransitioning = true;

    printerModel_->SetState(st);
    SendStateUpdate();

    bool result = rciClient_->StartJet();

    if (!result) {
        jetTransitioning_ = false;
        return false;
    }
    lastCommandTime_ = std::chrono::steady_clock::now();
    return true;
}

void AppController::HandleStopJetRequest() {
    if (!rciClient_ || !rciClient_->IsConnected()) return;
    if (jetTransitioning_) return;

    jetTransitioning_ = true; // 🔒 LOCK
    hasInitialStatus_ = false; // QUAN TRỌNG: reset;

    PrinterState st = printerModel_->GetState();
    st.status = PrinterStateType::StopingJet;
    st.statusText = L"Đang dừng Jet...";
    st.jetTransitioning = true;

    printerModel_->SetState(st);
    SendStateUpdate();

    if (!rciClient_->StopJet()) {
        jetTransitioning_ = false;
        return;
    }

    lastCommandTime_ = std::chrono::steady_clock::now();
    SendStateUpdate();
}

void AppController::HandleStartPrintRequest(const Request& req){
    if (!rciClient_ || !rciClient_->IsConnected()) {
        SendLogMessage(L"Chưa kết nối máy in", 2);
        return;
    }

    HandleStartJetRequest();// 1️⃣ Start Jet
    static int jobId = 1;// 2️⃣ Tạo message name duy nhất
    std::string msgName = "JOB" + std::to_string(jobId++);
    msgName.resize(8, '0'); // đảm bảo 8 ký tự
    // 3️⃣ Encode nội dung in
    auto fieldData = MessageBuilder().BuildSimpleTextField(req.data);
    // 4️⃣ Download field / message
    if (!rciClient_->DownloadRemoteField(fieldData)) {
        SendLogMessage(L"Lỗi DownloadMessage", 2);
        return;
    }
    // 5️⃣ Load message
    if (!rciClient_->LoadMessage(msgName, (uint16_t)req.count)) {
        SendLogMessage(L"Lỗi LoadMessage", 2);
        return;
    }
    // 6️⃣ Start print
    if (!rciClient_->StartPrint()) {
        SendLogMessage(L"Lỗi StartPrint", 2);
        return;
    }
    printerModel_->SetCurrentJob(req.data, req.count);
    SendLogMessage(L"Đã gửi job in mới (RCI chuẩn)", 0);
}

void AppController::HandleStopPrintRequest() {
    if (rciClient_ && rciClient_->IsConnected()) {
        if (!rciClient_->StopPrint()) {
            SendLogMessage(L"Lỗi gửi lệnh StopPrint", 2);
            return;
        }
    }
    SendLogMessage(L"Đã gửi lệnh dừng in, chờ máy in phản hồi...", 0);
}

void AppController::HandleUploadContentRequest(const Request& request) {
    int target = request.count;
    if (target <= 0) target = 1;
    printerModel_->SetCurrentJob(request.data, target);
    SendLogMessage( L"Đã cập nhật nội dung in và số lượng: " + std::to_wstring(target), 0);
    SavePrintDataOnExit();
}

void AppController::HandleConnectRequest(const Request& req){
    SendLogMessage(L"Đang kết nối...", 0);
    printerModel_->SetConnectionInfo(req.data, 9100);
    bool ok = rciClient_->Connect(req.data, 9100, 3000);
    PrinterState st = printerModel_->GetState();

    if (!ok)
    {
        EnableAutoReconnect();//BẬT LẠI AUTORECONNECT
        // 🔁 Nếu vẫn còn lượt thử lại → giữ nguyên trạng thái hiện tại (Connecting = vàng)
        if (autoReconnect_ && reconnectAttempts_ < MAX_RECONNECT_ATTEMPTS)
        {
            PrinterState st2 = st;
            st2.status = PrinterStateType::Reconnecting;
            st2.statusText = L"Đang kết nối lại...";
            printerModel_->SetState(st2);

            SendStateUpdate();
            return;
        }
        st.status = PrinterStateType::Error;
        st.statusText = L"Lỗi kết nối";
        printerModel_->SetState(st);
        printerModel_->SetStatusText(st.statusText);
        SendLogMessage(L"❌ Không thể kết nối", 2);
        SendStateUpdate();
        SendConnectionUpdate(false);
        return;
    }
	reconnectAttempts_ = 0; // reset lại bộ đếm nếu kết nối thành công
    SendStateUpdate();
    SendConnectionUpdate(true);
    SendLogMessage(L"✅ Đã kết nối tới máy in, đang chờ trạng thái STATUS 0x14...", 0);
    if (rciClient_ && rciClient_->IsConnected()) {
        HandleStatusRequest();   // sẽ cập nhật PrinterModel + SendStateUpdate()
    }
    else {
        SendStateUpdate();
    }
    EnableAutoReconnect();
}

void AppController::HandleSetCountRequest(const Request& request) {
    std::wstring currentContent = printerModel_->GetCurrentJobContent();
    printerModel_->SetCurrentJob(currentContent, request.count);
    SendLogMessage(L"Đã đặt số lượng in: " + std::to_wstring(request.count));
    SavePrintDataOnExit();
}

void AppController::TryReconnect() {
    if (reconnectAttempts_ >= MAX_RECONNECT_ATTEMPTS) return; // không log nữa
    auto lastIp = printerModel_->GetIpAddress();
    if (lastIp.empty()) return;

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
    return (!state.jetOn)
        && printerModel_->HasPendingPrintJob()
        && !jetTransitioning_; 
}

void AppController::UpdatePrinterState(){
    auto currentState = printerModel_->GetState();
    if (!rciClient_ || !rciClient_->IsConnected())// Nếu socket KHÔNG KẾT NỐI
    {
        // 1. Nếu đang autoReconnect và chưa quá số lần → báo Reconnecting
        if (autoReconnect_ && reconnectAttempts_ < MAX_RECONNECT_ATTEMPTS)
        {
            SendLogMessage(L"🔄 Đang kết nối lại...", 1);
            // KHÔNG đổi PrinterState (để không phá state Idle/Printing/Ready)
            SendConnectionUpdate(false);
            return;
        }
        // 2. Hết lượt hoặc autoReconnect tắt → báo Disconnected
        SendLogMessage(L"🔌 Mất kết nối với máy in", 2);
        return;
    }
}
// ================== THREAD-SAFE UI UPDATES ==================
void AppController::SendStateUpdate() {
    if (!mainWindow_) return;

    auto state = printerModel_->GetState();
    auto statusText = printerModel_->GetStatusText();
    // ✅ Lấy dữ liệu job/count từ model
    const auto& job = printerModel_->GetCurrentJobContent();
    int target = (int)state.targetCount;     // hoặc printerModel_->GetTargetCount()
    int printed = (int)state.printedCount;   // hoặc printerModel_->GetCurrentCount()

    auto* msg = new PrinterStateMessage{};
    msg->state = state;
    msg->statusText = statusText;
    msg->jobContent = job;
    msg->targetCount = target;
    msg->printedCount = printed;

    if (!PostMessage(mainWindow_, WM_APP_PRINTER_UPDATE, (WPARAM)msg, 0)) delete msg;
}

void AppController::SendLogMessage(const std::wstring& text, int level) {
    if (!mainWindow_) return;
    auto* msg = new LogMessage{ text, level };
    if (!PostMessage(mainWindow_, WM_APP_LOG, (WPARAM)msg, 0)) delete msg;
}

void AppController::SendConnectionUpdate(bool connected) {
    if (!mainWindow_) return;
    auto ip = printerModel_->GetIpAddress();
    auto* msg = new ConnectionMessage{ connected, ip, 9100 };
    if (!PostMessage(mainWindow_, WM_APP_CONNECTION_UPDATE, (WPARAM)msg, 0)) delete msg;
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

