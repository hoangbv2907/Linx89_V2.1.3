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

    // Manual cleanup method
    void EmergencyCleanup();
    void ComprehensiveCleanup();
    // Public API for UI - chỉ push request vào queue
    void Connect(const std::wstring& ipAddress);
    void Disconnect();
    void StartPrinting(const std::wstring& content, int count);
    void StopPrinting();
    void SetCount(int count);
    void StartJet();
    void StopJet();

    bool ValidatePrintContent(const std::wstring& content);
    bool ValidatePrintCount(int count);
    // State access
    PrinterState GetCurrentState() const;
    bool IsConnected() const;

    // Thread management
    void StartWorkerThread();
    bool StopWorkerThread(int timeoutMs);

private:
    ResourceTracker resourceTracker;
    HWND mainWindow_;
    std::unique_ptr<RciClient> rciClient_;
    std::unique_ptr<PrinterModel> printerModel_;

    // Worker thread và queue
    std::thread workerThread_;
    std::atomic<bool> running_{ false };
    RequestQueue requestQueue_;

    // State machine variables
    std::atomic<bool> autoReconnect_{ true };
    std::atomic<int> reconnectAttempts_{ 0 };
    const int MAX_RECONNECT_ATTEMPTS = 5;

    // Worker thread functions
    void WorkerLoop();
    void HandleRequest(const Request& request);
    void DoPeriodicPoll();
    void TryReconnect();

    // Request handlers
    void HandleStatusRequest();
    void HandlePrintCountRequest();
    void HandleStartPrintRequest(const Request& request);
    void HandleStopPrintRequest();

    void HandleSetCountRequest(const Request& request);
    bool HandleStartJetRequest();
    void HandleStopJetRequest();
    void HandleConnectRequest(const Request& request);
    void HandleDisconnectRequest();
    // State machine logic
    void UpdatePrinterState();
    bool ShouldPollStatus() const;
    bool ShouldGetPrintCount() const;
    bool ShouldAutoStartJet() const;

    // Message sending to UI (thread-safe)
    void SendStateUpdate();
    void SendLogMessage(const std::wstring& text, int level = 0);
    void SendConnectionUpdate(bool connected);
};