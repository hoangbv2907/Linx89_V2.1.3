
#include "RciClient.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include "AppController.h"

#include <iostream>  // THÊM DÒNG NÀY
#include <string>    // THÊM DÒNG NÀY

using namespace std;
#pragma comment(lib, "Ws2_32.lib")
// =========================================================
// Constructor / Destructor
// =========================================================
RciClient::RciClient() : sock_(INVALID_SOCKET), connected_(false), port_(0) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}

RciClient::~RciClient() {
    Disconnect();
    WSACleanup();
}
static std::wstring WsaErrorToString(int err) {
    wchar_t* msg = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&msg, 0, NULL);
    std::wstring out;
    if (msg) {
        out = msg;
        LocalFree(msg);
    }
    else {
        std::wstringstream ss; ss << L"WSA error " << err;
        out = ss.str();
    }
    return out;
}

// =========================================================
// Connection Management
// =========================================================

bool RciClient::Connect(const std::wstring& ip, unsigned short port, int timeoutMs) {
    std::lock_guard<std::mutex> lock(mtx_);

    Disconnect();

    host_ = ip;
    port_ = port;
    Log(L"🔌 [Connect] Bắt đầu kết nối đến " + ip + L":" + std::to_wstring(port), 1);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        Log(L"❌ Không thể tạo socket", 2);
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    char ipUtf8[64];
    WideCharToMultiByte(CP_ACP, 0, ip.c_str(), -1, ipUtf8, sizeof(ipUtf8), NULL, NULL);

    if (inet_pton(AF_INET, ipUtf8, &addr.sin_addr) <= 0) {
        Log(L"❌ Địa chỉ IP không hợp lệ", 2);
        closesocket(s);
        return false;
    }

    u_long mode = 1; ioctlsocket(s, FIONBIO, &mode);
    int res = connect(s, (sockaddr*)&addr, sizeof(addr));
    if (res == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            closesocket(s);
            Log(L"❌ Kết nối thất bại", 2);
            return false;
        }
    }

    fd_set set; FD_ZERO(&set); FD_SET(s, &set);
    timeval tv; tv.tv_sec = timeoutMs / 1000; tv.tv_usec = (timeoutMs % 1000) * 1000;

    res = select(0, NULL, &set, NULL, &tv);
    if (res <= 0) {
        closesocket(s);
        Log(L"⏰ Timeout kết nối", 2);
        return false;
    }
    // kiểm tra lỗi socket thực sự
    int so_err = 0;
    int optlen = sizeof(so_err);
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&so_err, &optlen) == 0) {
        if (so_err != 0) {
            closesocket(s);
            std::wstringstream ss; ss << L"❌ Kết nối thất bại (SO_ERROR=" << so_err << L")";
            Log(ss.str(), 2);
            return false;
        }
    }
    mode = 0; ioctlsocket(s, FIONBIO, &mode);
    sock_ = s;
    connected_ = true;

    return true;
}

// ==========================
// RciClient.cpp
// ==========================

bool RciClient::Disconnect()
{
    // Ngắt kết nối logic
    bool wasConnected = connected_.exchange(false);
    if (!wasConnected)
        return true;

    SOCKET localSock = INVALID_SOCKET;

    // ⚡ Chỉ lấy socket ra (nếu có thể lock), KHÔNG đợi mutex lâu
    if (mtx_.try_lock()) {
        localSock = sock_;
        sock_ = INVALID_SOCKET;
        mtx_.unlock();
    }
    else {
        Log(L"⚠️ [Disconnect] Mutex busy, sẽ force-close socket", 2);
        localSock = sock_;
        sock_ = INVALID_SOCKET;
    }

    // 🧩 Tắt socket ngoài mutex để tránh deadlock
    if (localSock != INVALID_SOCKET) {
        ::shutdown(localSock, SD_BOTH);
        ::closesocket(localSock);
        Log(L"🔌 [Disconnect] Socket closed (safe immediate)", 1);
    }

    return true;
}


bool RciClient::IsConnected() const {
    return connected_;
}

// =========================================================
// Send / Receive
// =========================================================
bool RciClient::SendFrame(const vector<uint8_t>& frame, vector<uint8_t>& reply, int timeoutMs) {
    std::lock_guard<std::mutex> lock(mtx_);

    if (!connected_ || sock_ == INVALID_SOCKET)
        return false;
    //Kiểm tra kết nối trước khi gửi
    if (!IsConnected()) {
        return false;
    }
    // 🟢 TĂNG hiệu suất gửi
    u_long mode = 1; // non-blocking
    ioctlsocket(sock_, FIONBIO, &mode);

    if (!SendRaw(frame)) {
        mode = 0; // blocking
        ioctlsocket(sock_, FIONBIO, &mode);
        return false;
    }

    bool result = ReceiveRaw(reply, timeoutMs);

    mode = 0; // blocking
    ioctlsocket(sock_, FIONBIO, &mode);

    return result;
}

bool RciClient::SendRaw(const vector<uint8_t>& buf) {
    int sent = send(sock_, (const char*)buf.data(), (int)buf.size(), 0);
    if (sent == SOCKET_ERROR) {
        Log(L"❌ Lỗi gửi dữ liệu", 2);
        return false;
    }
    return true;
}

bool RciClient::ReceiveRaw(vector<uint8_t>& buf, int timeoutMs) {
    buf.clear();
    const int CHUNK = 1024;
    char tmp[CHUNK];

    // tổng timeout xử lý theo vòng lặp
    auto t0 = std::chrono::steady_clock::now();
    int remaining = timeoutMs;

    // buffer tạm để tìm kết thúc frame
    std::vector<uint8_t> acc;

    if (!connected_) return false;

    while (true) {
        if (!connected_ || sock_ == INVALID_SOCKET) {
            Log(L"🧩 Socket đã bị đóng — thoát ReceiveRaw()", 1);
            return false;
        }

        fd_set set;
        FD_ZERO(&set);
        FD_SET(sock_, &set);
        timeval tv;
        tv.tv_sec = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        int res = select(0, &set, NULL, NULL, &tv);
        if (res == SOCKET_ERROR) {
            Log(L"❌ Lỗi select, socket không hợp lệ", 2);
            connected_ = false;
            return false;
        }

        if (res == 0) { // timeout
            if (!connected_) return false;
            continue;
        }

        // socket đã sẵn sàng
        int received = recv(sock_, tmp, CHUNK, 0);
        if (received <= 0) {
            Log(L"🔌 Socket đóng hoặc mất kết nối (recv <= 0)", 2);
            connected_ = false;
            return false;
        }
    }
}

// =========================================================
// RCI Command Builders
// =========================================================
uint8_t RciClient::ComputeChecksum(const vector<uint8_t>& bytes) {
    unsigned int sum = 0;
    for (auto b : bytes) sum += b;
    return (uint8_t)((0x100 - (sum & 0xFF)) & 0xFF);
}

vector<uint8_t> RciClient::BuildFrame(uint8_t commandId, const vector<uint8_t>& payload, bool useSOH, bool includeChecksum) {
    vector<uint8_t> frame;
    const uint8_t ESC = 0x1B, STX = 0x02, SOH = 0x01, ETX = 0x03;

    frame.push_back(ESC);
    frame.push_back(useSOH ? SOH : STX);

    vector<uint8_t> body;
    body.push_back(commandId);
    body.insert(body.end(), payload.begin(), payload.end());

    for (auto b : body) {
        if (b == ESC || b == STX || b == SOH || b == ETX) {
            frame.push_back(ESC);
            frame.push_back(b);
        }
        else {
            frame.push_back(b);
        }
    }

    frame.push_back(ESC);
    frame.push_back(ETX);

    if (includeChecksum) {
        vector<uint8_t> csData;
        csData.push_back(useSOH ? SOH : STX);
        csData.insert(csData.end(), body.begin(), body.end());
        csData.push_back(ETX);
        frame.push_back(ComputeChecksum(csData));
    }

    return frame;
}

// =========================================================
// High-level LINX Commands
// =========================================================
bool RciClient::RequestStatus() {
    vector<uint8_t> reply;
    auto frame = BuildFrame(0x14);
    return SendFrame(frame, reply);
}

bool RciClient::StartPrint() {
    vector<uint8_t> reply;
    auto frame = BuildFrame(0x11);
    return SendFrame(frame, reply);
}

bool RciClient::StopPrint() {
    vector<uint8_t> reply;
    auto frame = BuildFrame(0x12);
    return SendFrame(frame, reply);
}

bool RciClient::StartJet() {
    vector<uint8_t> reply;
    auto frame = BuildFrame(0x0F);
    return SendFrame(frame, reply);
}
bool RciClient::StopJet() {
    vector<uint8_t> reply;
    auto frame = BuildFrame(0x10);
    return SendFrame(frame, reply);
}

bool RciClient::LoadMessage(const string& name, uint16_t printCount) {
    vector<uint8_t> payload;
    string fixed = name;
    fixed.resize(8, '\0');
    payload.insert(payload.end(), fixed.begin(), fixed.end());
    payload.push_back(printCount & 0xFF);
    payload.push_back((printCount >> 8) & 0xFF);

    vector<uint8_t> reply;
    return SendFrame(BuildFrame(0x1E, payload), reply);
}

bool RciClient::DownloadRemoteField(const vector<uint8_t>& data) {
    vector<uint8_t> payload;
    uint16_t len = data.size();
    payload.push_back(len & 0xFF);
    payload.push_back((len >> 8) & 0xFF);
    payload.insert(payload.end(), data.begin(), data.end());

    vector<uint8_t> reply;
    return SendFrame(BuildFrame(0x1D, payload), reply);
}

bool RciClient::DownloadMessageData(const vector<uint8_t>& data) {
    vector<uint8_t> reply;
    return SendFrame(BuildFrame(0x19, data), reply);
}

// =========================================================
// Extended high-level utilities for AppController
// =========================================================
bool RciClient::SendAndWaitAck(uint8_t cmdid, const std::vector<uint8_t>& payload, int timeoutMs)
{
    if (timeoutMs <= 0) {
        if (cmdid == 0x0F || cmdid == 0x10) timeoutMs = 60000;
        else timeoutMs = 3000;
    }

    std::vector<uint8_t> reply;
    if (!SendFrame(BuildFrame(cmdid, payload), reply, timeoutMs))
        return false;

    Log(L"Recv: " + ReplyToString(reply), 1);

    if (reply.size() < 5) return false;
    const uint8_t ESC = 0x1B, ACK = 0x06, NAK = 0x15;

    if (reply[0] != ESC) return false;

    if (reply[1] == NAK) return false;
    if (reply[1] != ACK) return false;

    // --- parse frame body robustly: find delimiter (STX or SOH) at offset 1 already known? ---
    // find first STX or SOH (reply[1] is ACK, so STX/SOH is not here) 
    // We need to extract the body bytes between delimiter and ESC ETX.
    // Find the first occurrence of ESC + ETX from the end.
    size_t esc_etx_pos = std::string::npos;
    for (size_t i = 0; i + 1 < reply.size(); ++i) {
        if (reply[i] == 0x1B && reply[i + 1] == 0x03) { esc_etx_pos = i; break; }
    }
    if (esc_etx_pos == std::string::npos) {
        // cannot find end marker, fallback to previous simple check
        uint8_t recvCmd = reply.size() > 4 ? reply[4] : 0x00;
        return recvCmd == cmdid;
    }

    // body is bytes between reply[2] ... before esc_etx_pos
    // But need to unescape bytes (ESC prefix)
    std::vector<uint8_t> body;
    // body starts at index 2 (p-status at [2])
    for (size_t i = 2; i < esc_etx_pos; ++i) {
        if (reply[i] == 0x1B) {
            if (i + 1 < esc_etx_pos) {
                body.push_back(reply[i + 1]);
                ++i;
            }
            else {
                // malformed escape, break
                return false;
            }
        }
        else {
            body.push_back(reply[i]);
        }
    }

    // body layout: [p_status, c_status, cmdid, ...]
    if (body.size() < 3) return false;
    uint8_t recvCmd = body[2];

    // verify checksum: compute over (ACK) + body + ETX
    std::vector<uint8_t> csArea;
    csArea.push_back(reply[1]); // ACK or NAK byte
    // append raw body (unescaped) but the protocol checksum computed on unescaped body bytes
    csArea.insert(csArea.end(), body.begin(), body.end());
    csArea.push_back(0x03); // ETX
    uint8_t expected = ComputeChecksum(csArea);

    // determine received checksum (after ESC ETX)
    // position after esc_etx_pos + 2
    size_t after_etx = esc_etx_pos + 2;
    if (after_etx >= reply.size()) return false;
    uint8_t recvChk = 0;
    if (reply[after_etx] == 0x1B) {
        if (after_etx + 1 >= reply.size()) return false;
        recvChk = reply[after_etx + 1];
    }
    else {
        recvChk = reply[after_etx];
    }
    if (recvChk != expected) {
        Log(L"⚠ Checksum mismatch on reply", 1);
        return false;
    }

    return recvCmd == cmdid;
}


PrinterStatus RciClient::RequestStatusEx() {
    std::vector<uint8_t> reply;
    PrinterStatus s;

    if (!IsConnected()) return s;

    if (!SendFrame(BuildFrame(0x14), reply, 100))
        return s;

    const uint8_t ESC = 0x1B, ACK = 0x06;
    if (reply.size() >= 13 && reply[0] == ESC && reply[1] == ACK) {
        s.jetState = reply[5];
        s.printState = reply[6];
        s.errorMask = (reply[7] << 24) | (reply[8] << 16) | (reply[9] << 8) | reply[10];

        s.jetOn = (s.jetState != 0x03); // 03 = tắt jet
        s.printing = (s.printState == 0x04); // 04 = đang in
        s.paused = (s.printState == 0x02); // 02 = tạm dừng
    }
    return s;
}
// =========================================================
// Utility
// =========================================================
void RciClient::Log(const std::wstring& msg, int type) {
    if (callback_) callback_(msg, type);
}

std::wstring RciClient::ReplyToString(const vector<uint8_t>& reply) {
    wstringstream ws;
    ws << L"[" << reply.size() << L" bytes] ";
    for (size_t i = 0; i < min(reply.size(), size_t(12)); ++i)
        ws << hex << setw(2) << setfill(L'0') << (int)reply[i] << L" ";
    return ws.str();
}

bool RciClient::SendCommandNoAck(uint8_t cmdid, const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<uint8_t> frame = BuildFrame(cmdid, payload);
    return SendRaw(frame);
}
