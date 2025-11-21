
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <functional>

struct PrinterStatus {
    uint8_t jetState = 0;
    uint8_t printState = 0;
    uint32_t errorMask = 0;
    bool jetOn = false;
    bool printing = false;
    bool paused = false;
};

class RciClient {
public:
    using MessageCallback = std::function<void(const std::wstring&, int)>;

    RciClient();
    ~RciClient();

    // Connection
    bool Connect(const std::wstring& ip, unsigned short port = 9100, int timeoutMs = 3000);
    bool Disconnect();
    bool IsConnected() const;

    // Command send/receive
    bool SendFrame(const std::vector<uint8_t>& frame, std::vector<uint8_t>& reply, int timeoutMs = 3000);

    // High-level RCI commands
    bool RequestStatus();
    bool StartPrint();
    bool StopPrint();
    bool StartJet();
    bool StopJet();

    bool LoadMessage(const std::string& name, uint16_t printCount = 1);
    bool DownloadRemoteField(const std::vector<uint8_t>& data);
    bool DownloadMessageData(const std::vector<uint8_t>& messageData);

    // Extended high-level utilities for AppController
    bool SendAndWaitAck(uint8_t cmdid, const std::vector<uint8_t>& payload, int timeoutMs = 3000);
    PrinterStatus RequestStatusEx();

    // Frame builders (static)
    static std::vector<uint8_t> BuildFrame(uint8_t commandId, const std::vector<uint8_t>& payload = {},
        bool useSOH = false, bool includeChecksum = true);

    // Utility
    static uint8_t ComputeChecksum(const std::vector<uint8_t>& bytes);
    static std::wstring ReplyToString(const std::vector<uint8_t>& reply);

    void SetMessageCallback(MessageCallback cb) { callback_ = cb; }

    // =====================================================
    // Gửi lệnh thô (không chờ ACK)
    // =====================================================
    bool SendCommandNoAck(uint8_t cmdid, const std::vector<uint8_t>& payload = {});

private:
    SOCKET sock_;
    std::atomic<bool> connected_;
    std::wstring host_;
    unsigned short port_;
    std::mutex mtx_;

    MessageCallback callback_;

    void Log(const std::wstring& msg, int type = 0);
    bool SendRaw(const std::vector<uint8_t>& buf);
    bool ReceiveRaw(std::vector<uint8_t>& buf, int timeoutMs);
};
