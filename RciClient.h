#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include "ThreadSafeQueue.h"

#pragma comment(lib, "ws2_32.lib")

class RciClient {
public:
    RciClient();
    ~RciClient();

    bool Connect(const std::wstring& ip, int port = 9100);
    void Disconnect();
    bool IsConnected() const { return connected_; }

    bool StartJet();
    bool StopJet();
    bool PrintText(const std::wstring& text, int copies = 1);
    bool GetStatusEx(std::wstring& status);
    bool SendCustomCommand(const std::wstring& command);

    void SetMessageCallback(std::function<void(const std::wstring&, int)> callback) {
        messageCallback_ = callback;
    }

private:
    SOCKET socket_ = INVALID_SOCKET;
    std::wstring currentIp_;
    int currentPort_ = 0;
    std::atomic<bool> connected_{ false };
    std::atomic<bool> running_{ false };

    std::function<void(const std::wstring&, int)> messageCallback_;

    std::thread readerThread_;
    void ReaderLoop();

    bool InitializeWinsock();
    void CleanupWinsock();
    bool SendData(const std::vector<uint8_t>& data);
    bool ReceiveData(std::vector<uint8_t>& buffer, int timeoutMs = 5000);

    std::vector<uint8_t> BuildRciFrame(const std::wstring& command);
    bool ParseRciResponse(const std::vector<uint8_t>& data, std::wstring& result);

    void Log(const std::wstring& message, int level = 0) {
        if (messageCallback_) {
            messageCallback_(message, level);
        }
    }
};