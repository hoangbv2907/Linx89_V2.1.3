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
#include "RciClient.h"
#include "PrinterModel.h"
#include "MessageDef.h"
#include "ThreadSafeQueue.h"
#include "CommonTypes.h"
#include "RequestQueue.h"
#include "ResourceTracker.h"
// Forward declarations
class RciClient;
class PrinterModel;

class AppController {
public:
    AppController(HWND mainWindow);
    ~AppController();

	//==== Cleanup routines ====
    // đóng socket ngay lập tức, dừng jet, reset state
    void EmergencyCleanup();
    //tắt ứng dụng bình thường
    void ComprehensiveCleanup();

	//=====Public API for UI - chỉ push request vào queue=====
    void Connect(const std::wstring& ipAddress);
    void Disconnect();
    void StartPrinting(const std::wstring& content, int count);
    void StopPrinting();
    void SetCount(int count);
    void StartJet();
    void StopJet();

	//===== Validation methods===
    bool ValidatePrintContent(const std::wstring& content);
    bool ValidatePrintCount(int count);
	//======= Getter methods =====
    PrinterState GetCurrentState() const; //Lấy trạng thái đang lưu trong PrinterModel
	bool IsConnected() const;   //Kiểm tra trạng thái kết nối từ RciClient

	//================= WORKER THREAD MANAGEMENT =================
	void StartWorkerThread();   //khởi động worker thread
	bool StopWorkerThread(int timeoutMs);   //dừng worker thread với timeout

private:
	ResourceTracker resourceTracker;    // Quản lý cleanup resources
	HWND mainWindow_;   // Handle của cửa sổ chính
	std::unique_ptr<RciClient> rciClient_;  // Client RCI để giao tiếp với máy in
	std::unique_ptr<PrinterModel> printerModel_;    // Model lưu trạng thái máy in

	//=== Worker thread and request queue ====
	std::thread workerThread_;  // Thread xử lý nền
	std::atomic<bool> running_{ false };    // Biến điều khiển vòng lặp worker thread
	RequestQueue requestQueue_; // Queue chứa các request từ UI

	//== Reconnect management ==
	std::atomic<bool> autoReconnect_{ true };   // Tự động reconnect khi mất kết nối
	std::atomic<int> reconnectAttempts_{ 0 };   // Số lần đã thử reconnect
	const int MAX_RECONNECT_ATTEMPTS = 5;   // Giới hạn số lần reconnect

	//== Worker thread methods ==
	void WorkerLoop();  // Vòng lặp chính của worker thread
	void HandleRequest(const Request& request); // Xử lý từng request cụ thể
	void DoPeriodicPoll();  // Poll trạng thái định kỳ
	void TryReconnect();    // Thử reconnect nếu mất kết nối

	//==== Request Handlers =====
	void HandleStatusRequest();     // Xử lý yêu cầu lấy trạng thái
	void HandlePrintCountRequest();     // Xử lý yêu cầu lấy số lượng đã in
	void HandleStartPrintRequest(const Request& request);   // Xử lý yêu cầu bắt đầu in
	void HandleStopPrintRequest();  // Xử lý yêu cầu dừng in

	void HandleSetCountRequest(const Request& request);		// Xử lý yêu cầu đặt số lượng in
	bool HandleStartJetRequest();	// Xử lý yêu cầu bật jet
	void HandleStopJetRequest();	// Xử lý yêu cầu tắt jet
	void HandleConnectRequest(const Request& request);	// Xử lý yêu cầu kết nối
	void HandleDisconnectRequest();	// Xử lý yêu cầu ngắt kết nối
	//==State machine logic==
	void UpdatePrinterState();	// Cập nhật trạng thái máy in theo state machine
	bool ShouldPollStatus() const;	// Kiểm tra có nên poll trạng thái không
	bool ShouldGetPrintCount() const;	// Kiểm tra có nên lấy số lượng in không
	bool ShouldAutoStartJet() const;	// Kiểm tra có nên tự động bật jet không

	//=================== Messaging to UI ==================
	void SendStateUpdate();		// Gửi cập nhật trạng thái máy in tới UI
	void SendLogMessage(const std::wstring& text, int level = 0);	// Gửi thông điệp log tới UI
	void SendConnectionUpdate(bool connected);	// Gửi cập nhật trạng thái kết nối tới UI
};