#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <thread>
#include <atomic>
#include <memory>
#include <functional>
#include <string>
#include <set>
#include <chrono>
#include <vector>

#include "RciClient.h"
#include "PrinterModel.h"
#include "MessageDef.h"
#include "ThreadSafeQueue.h"
#include "CommonTypes.h"
#include "RequestQueue.h"
#include "ResourceTracker.h"

class RciClient;
class PrinterModel;

class AppController {
public:
    // ================== Lifecycle / Ownership ==================
    AppController(HWND mainWindow);   // Constructor
    ~AppController();                 // Destructor

    // ================== Worker thread management ==================
    void StartWorkerThread();               // Tạo và chạy thread nền (WorkerLoop)
    bool StopWorkerThread(int timeoutMs);   // Yêu cầu dừng và join thread trong timeout

    // ================== Cleanup routines ==================
    // Cleanup khẩn cấp: dừng worker càng nhanh càng tốt, force close socket, reset state
    // (StopJet chỉ best-effort nếu còn kết nối)
    void EmergencyCleanup();

    // Shutdown bình thường: stop worker tuần tự, disconnect, giải phóng tài nguyên
    void ComprehensiveCleanup();

    // ================== UI Facade (enqueue request only) ==================
    // Lưu ý: các hàm này KHÔNG làm socket trực tiếp, chỉ push request vào queue
    void Connect(const std::wstring& ipAddress);                 // Enqueue: kết nối
    void Disconnect();                                           // Enqueue: ngắt kết nối

    void UploadContent(const std::wstring& content, int count);  // Enqueue: cập nhật nội dung + số lượng
    void SetCount(int count);                                    // Enqueue: đặt target count

    void StartJet();                                             // Enqueue: bật jet
    void StopJet();                                              // Enqueue: tắt jet

    void StartPrinting(const std::wstring& content, int count);  // Enqueue: bắt đầu in (có thể gồm upload + start)
    void StopPrinting();                                         // Enqueue: dừng in

    // Enqueue: gửi Remote Field Data by Name (thường RCI 0x9E)
    void UploadRemoteFieldData(const std::wstring& fieldName, const std::wstring& value);

    // ================== Validation / Policy ==================
    bool ValidatePrintContent(const std::wstring& content);      // Kiểm tra nội dung in hợp lệ
    bool ValidatePrintCount(int count);                          // Kiểm tra số lượng in hợp lệ

    void DisableAutoReconnect() { autoReconnect_ = false; }      // Tắt tự động reconnect
    void EnableAutoReconnect() { autoReconnect_ = true; }        // Bật tự động reconnect

    // ================== Getters ==================
    PrinterState GetCurrentState() const;                        // Lấy trạng thái hiện tại trong PrinterModel
    bool IsConnected() const;                                    // Trạng thái kết nối (từ RciClient)

    void SetLastIp(const std::wstring& ip);                      // Lưu IP kết nối gần nhất (config/persist)

    // ================== Data persistence ==================
    void SavePrintDataOnExit();                                  // Lưu last_print_data khi đóng ứng dụng
    void LoadPrintDataOnStart();                                 // Load last_print_data khi khởi động

    // ================== Messaging / UI updates ==================
    // Đóng gói state hiện tại từ PrinterModel và PostMessage lên UI (WM_APP_PRINTER_UPDATE)
    void SendStateUpdate();

private:
    // ================== Owned resources ==================
    ResourceTracker resourceTracker;               // Quản lý cleanup resources theo scope
    HWND mainWindow_;                              // Handle cửa sổ chính để PostMessage
    std::unique_ptr<RciClient> rciClient_;         // Client RCI Linx
    std::unique_ptr<PrinterModel> printerModel_;   // Model lưu trạng thái máy in / job

    // ================== Worker thread & request queue ==================
    std::thread workerThread_;                     // Thread xử lý nền
    std::atomic<bool> running_{ false };           // Cờ điều khiển vòng lặp worker
    RequestQueue requestQueue_;                    // Queue chứa các request từ UI

    // Timestamp lệnh RCI gần nhất (dùng throttle/giãn cách gửi lệnh để tránh spam)
    std::chrono::steady_clock::time_point lastCommandTime_;

    // True khi đang chờ jet chuyển trạng thái (starting/stopping) để tránh gửi lệnh chồng
    std::atomic<bool> jetTransitioning_{ false };

    // ================== Reconnect management ==================
    std::atomic<bool> autoReconnect_{ true };      // Tự động reconnect khi mất kết nối
    std::atomic<int> reconnectAttempts_{ 0 };      // Số lần đã thử reconnect
    const int MAX_RECONNECT_ATTEMPTS = 5;          // Giới hạn số lần reconnect

    // ================== State / Delta tracking ==================
    bool lastJetOn_ = false;                       // jetOn lần trước để phát hiện thay đổi (delta)
    bool hasInitialStatus_ = false;                // đã nhận status lần đầu sau connect chưa
    bool persistLoaded_ = false;                   // đã load dữ liệu persist trong phiên này chưa

    uint8_t  lastPStatus_ = 0;                     // P-STATUS lần trước (delta log)
    uint8_t  lastCStatus_ = 0;                     // C-STATUS lần trước (delta log)
    uint32_t lastErrorMask_ = 0;                   // errorMask lần trước (delta log)
    bool     hasLastStatus_ = false;               // đã có status trước đó để so sánh delta chưa
    std::vector<std::wstring> lastWarnings_;       // danh sách cảnh báo lần trước (decoded text)
    PrinterStatus lastStatus_{};                   // snapshot PrinterStatus lần trước

    // ================== Worker loop helpers ==================
    void WorkerLoop();                             // Vòng lặp chính của worker
    void HandleRequest(const Request& request);    // Dispatch xử lý từng request
    void DoPeriodicPoll();                         // Poll định kỳ (status/print count) khi connected
    void TryReconnect();                           // Thử reconnect nếu mất kết nối

    // ================== Request handlers ==================
    // Poll/status
    void HandleStatusRequest();                    // RCI STATUS 0x14
    // Đồng bộ print count (thường: đọc/refresh số lượng đã in từ máy qua RCI)
    void HandlePrintCountRequest();

    // Print
    void HandleStartPrintRequest(const Request& request);    // Start printing
    void HandleStopPrintRequest();                           // Stop printing

    // Job/content/count
    void HandleUploadContentRequest(const Request& request); // Upload nội dung + target count
    void HandleSetCountRequest(const Request& request);      // Set target count
    void HandleUploadRemoteFieldRequest(const Request& req); // Upload nội dung Remote field data by name

    // Jet
    bool HandleStartJetRequest();                    // Start jet (best-effort ACK)
    void HandleStopJetRequest();                     // Stop jet

    // Connection
    void HandleConnectRequest(const Request& request);// Connect
    void HandleDisconnectRequest();                  // Disconnect

    // ================== State machine / policy ==================
    void UpdatePrinterState();                       // Cập nhật state machine từ status/raw flags
    bool ShouldPollStatus() const;                   // Quyết định có poll status không
    bool ShouldGetPrintCount() const;                // Quyết định có poll số lượng đã in không
    bool ShouldAutoStartJet() const;                 // Quyết định có auto start jet không

    // ================== Messaging to UI ==================
    void SendLogMessage(const std::wstring& text, int level = 0); // Post log message lên UI
    void SendConnectionUpdate(bool connected);                    // Post trạng thái kết nối lên UI
};
