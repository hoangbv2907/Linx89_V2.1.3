#include "RciClient.h"
#include "Logger.h"
#include <iostream>
#include <algorithm>
#include <WS2tcpip.h> // Thêm cho inet_pton

RciClient::RciClient() {
    InitializeWinsock();
}

RciClient::~RciClient() {
    Disconnect();
    CleanupWinsock();
}

bool RciClient::InitializeWinsock() {
    WSADATA wsaData;
    WORD version = MAKEWORD(2, 2);
    int result = WSAStartup(version, &wsaData);
    if (result != 0) {
        Logger::GetInstance().Write(L"WSAStartup failed: " + std::to_wstring(result), 2);
        return false;
    }

    // Verify WinSock version 2.2
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        Logger::GetInstance().Write(L"WinSock version 2.2 not available", 2);
        WSACleanup();
        return false;
    }

    return true;
}

void RciClient::CleanupWinsock() {
    WSACleanup();
}

bool RciClient::Connect(const std::wstring& ip, int port) {
    if (connected_) {
        Disconnect();
    }

    // Convert wide string to narrow string for IP address
    int wideLen = (int)ip.length();
    int narrowLen = WideCharToMultiByte(CP_UTF8, 0, ip.c_str(), wideLen, nullptr, 0, nullptr, nullptr);
    std::string narrowIp(narrowLen, 0);
    WideCharToMultiByte(CP_UTF8, 0, ip.c_str(), wideLen, &narrowIp[0], narrowLen, nullptr, nullptr);

    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
        int error = WSAGetLastError();
        Logger::GetInstance().Write(L"Socket creation failed, error: " + std::to_wstring(error), 2);
        return false;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    // Use inet_pton for IP address conversion
    if (inet_pton(AF_INET, narrowIp.c_str(), &serverAddr.sin_addr) != 1) {
        Logger::GetInstance().Write(L"Invalid IP address: " + ip, 2);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    // Set timeout
    DWORD timeout = 5000; // 5 seconds
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

    if (connect(socket_, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        Logger::GetInstance().Write(L"Connection failed to " + ip + L":" + std::to_wstring(port) +
            L", error: " + std::to_wstring(error), 2);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    connected_ = true;
    currentIp_ = ip;
    currentPort_ = port;

    // Start background reader thread
    running_ = true;
    readerThread_ = std::thread(&RciClient::ReaderLoop, this);

    Logger::GetInstance().Write(L"Connected to " + ip + L":" + std::to_wstring(port));
    return true;
}

void RciClient::Disconnect() {
    running_ = false;
    connected_ = false;

    if (readerThread_.joinable()) {
        readerThread_.join();  // ✅ Đợi thread kết thúc
    }

    if (socket_ != INVALID_SOCKET) {
        shutdown(socket_, SD_BOTH);  // ✅ Ngừng nhận/gửi
        closesocket(socket_);        // ✅ Đóng socket
        socket_ = INVALID_SOCKET;
    }
    Logger::GetInstance().Write(L"Disconnected from printer");
}

void RciClient::ReaderLoop() {
    std::vector<uint8_t> buffer(4096);

    while (running_ && connected_) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket_, &readSet);

        timeval timeout{ 0, 100000 }; // 100ms

        int result = select(0, &readSet, nullptr, nullptr, &timeout);
        if (result > 0 && FD_ISSET(socket_, &readSet)) {
            int bytesReceived = recv(socket_, (char*)buffer.data(), (int)buffer.size(), 0);
            if (bytesReceived > 0) {
                // Process received data
                std::wstring message = L"Received " + std::to_wstring(bytesReceived) + L" bytes from printer";
                Log(message, 3); // Debug level

                // Parse printer response if needed
                std::wstring response;
                if (ParseRciResponse(buffer, response)) {
                    Log(L"Printer response: " + response, 3);
                }
            }
            else if (bytesReceived == 0) {
                Log(L"Printer disconnected", 1);
                connected_ = false;
                break;
            }
            else {
                int error = WSAGetLastError();
                if (error != WSAEWOULDBLOCK) {
                    Log(L"Socket error: " + std::to_wstring(error), 2);
                    connected_ = false;
                    break;
                }
            }
        }
    }
}

bool RciClient::SendData(const std::vector<uint8_t>& data) {
    if (!connected_) return false;

    int bytesSent = send(socket_, (const char*)data.data(), (int)data.size(), 0);
    if (bytesSent == SOCKET_ERROR) {
        int error = WSAGetLastError();
        Log(L"Send data failed, error: " + std::to_wstring(error), 2);
        connected_ = false;
        return false;
    }

    return bytesSent == data.size();
}

bool RciClient::StartJet() {
    Log(L"Sending StartJet command to printer");

    // Simulate RCI command - in real implementation, send actual RCI frame
    std::wstring command = L"\x02STARTJET\x03";
    auto frame = BuildRciFrame(command);

    if (SendData(frame)) {
        Log(L"StartJet command sent successfully");
        return true;
    }

    return false;
}

bool RciClient::StopJet() {
    Log(L"Sending StopJet command to printer");

    // Simulate RCI command - in real implementation, send actual RCI frame
    std::wstring command = L"\x02STOPJET\x03";
    auto frame = BuildRciFrame(command);

    if (SendData(frame)) {
        Log(L"StopJet command sent successfully");
        return true;
    }

    return false;
}

bool RciClient::PrintText(const std::wstring& text, int copies) {
    Log(L"Printing text: " + text + L" copies: " + std::to_wstring(copies));

    // Simulate printing process
    for (int i = 0; i < copies; i++) {
        std::wstring copyMsg = L"Printing copy " + std::to_wstring(i + 1) + L" of " + std::to_wstring(copies);
        Log(copyMsg);

        // In real implementation, send actual print command
        std::wstring command = L"\x02PRINT\x03" + text;
        auto frame = BuildRciFrame(command);

        if (!SendData(frame)) {
            Log(L"Failed to send print data for copy " + std::to_wstring(i + 1), 2);
            return false;
        }

        // Small delay between copies
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    Log(L"Print job completed successfully");
    return true;
}

bool RciClient::GetStatusEx(std::wstring& status) {
    if (!connected_) {
        status = L"DISCONNECTED";
        return false;
    }

    // Simulate status request
    std::wstring command = L"\x02STATUS\x03";
    auto frame = BuildRciFrame(command);

    if (SendData(frame)) {
        // In real implementation, wait for and parse response
        status = L"READY";
        return true;
    }

    status = L"ERROR";
    return false;
}

bool RciClient::SendCustomCommand(const std::wstring& command) {
    Log(L"Sending custom command: " + command);

    auto frame = BuildRciFrame(command);
    return SendData(frame);
}

std::vector<uint8_t> RciClient::BuildRciFrame(const std::wstring& command) {
    // Convert wide string to UTF-8
    int wideLen = (int)command.length();
    int narrowLen = WideCharToMultiByte(CP_UTF8, 0, command.c_str(), wideLen, nullptr, 0, nullptr, nullptr);
    std::string narrowCommand(narrowLen, 0);
    WideCharToMultiByte(CP_UTF8, 0, command.c_str(), wideLen, &narrowCommand[0], narrowLen, nullptr, nullptr);

    std::vector<uint8_t> frame;

    // Start with STX
    frame.push_back(0x02);

    // Add command bytes
    for (char c : narrowCommand) {
        frame.push_back(static_cast<uint8_t>(c));
    }

    // End with ETX
    frame.push_back(0x03);

    // Calculate checksum (simple XOR for example)
    uint8_t checksum = 0;
    for (uint8_t byte : frame) {
        checksum ^= byte;
    }
    frame.push_back(checksum);

    return frame;
}

bool RciClient::ParseRciResponse(const std::vector<uint8_t>& data, std::wstring& result) {
    if (data.empty()) {
        return false;
    }

    // Simple parsing - convert bytes back to wide string
    std::string narrowResult;
    for (size_t i = 0; i < data.size(); ++i) {
        uint8_t byte = data[i];
        // Skip control characters in display
        if (byte >= 32 && byte <= 126) { // Printable ASCII
            narrowResult += static_cast<char>(byte);
        }
    }

    // Convert narrow string to wide string
    int narrowLen = (int)narrowResult.length();
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, narrowResult.c_str(), narrowLen, nullptr, 0);
    result.resize(wideLen);
    MultiByteToWideChar(CP_UTF8, 0, narrowResult.c_str(), narrowLen, &result[0], wideLen);

    return true;
}