
#include "RciClient.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include "AppController.h"
#include <iostream> 
#include <string>    

using namespace std;
#pragma comment(lib, "Ws2_32.lib")

static void AppendNullTerminated(std::vector<uint8_t>& out, const std::string& s, size_t maxBytesNoNull) {
    size_t n = min(s.size(), maxBytesNoNull);
    out.insert(out.end(), s.begin(), s.begin() + n);
    out.push_back(0x00); // null terminator
}

static std::vector<uint8_t> BuildMsgName16(const std::string& nameOpt) {
    std::vector<uint8_t> buf(16, 0x00);
    if (!nameOpt.empty()) {
        size_t n = std::min<size_t>(15, nameOpt.size());
        memcpy(buf.data(), nameOpt.data(), n);
        buf[n] = 0x00;
    }
    return buf;
}

static std::wstring HexDump(const std::vector<uint8_t>& v, size_t max = 128) {
    std::wstringstream ws;
    ws << std::hex << std::setfill(L'0');
    size_t n = min(v.size(), max);
    for (size_t i = 0; i < n; ++i) ws << std::setw(2) << (int)v[i] << L' ';
    if (v.size() > n) ws << L"...";
    return ws.str();
}

static void UnescapeBodyBytes(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(in.size());

    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == 0x1B) {
            if (i + 1 >= in.size()) break; // malformed
            uint8_t b = in[i + 1];

            // ✅ Chỉ unescape các byte mà sender có quyền escape
            if (b < 0x20 || b == 0x1B) {
                out.push_back(b);
                ++i;
                continue;
            }

            // Nếu gặp ESC + byte không hợp lệ theo rule -> coi như dữ liệu thô
            // (hoặc bạn có thể return lỗi)
            out.push_back(0x1B);
            continue;
        }
        out.push_back(in[i]);
    }
}

static inline uint32_t ReadU32_LSB_First(const uint8_t* p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

struct ParsedReply {
    bool ok = false;
    bool ack = false; // true=ACK, false=NAK
    uint8_t p_status = 0, c_status = 0, cmdid = 0;
    std::vector<uint8_t> data;
};

static bool TryGetCmdIdFromFrame(const std::vector<uint8_t>& frame, uint8_t& outCmd) {
    if (frame.size() < 3) return false;
    if (frame[0] != 0x1B) return false;
    if (frame[1] != 0x01 && frame[1] != 0x02) return false; // SOH/STX
    if (frame[2] == 0x1B) { // double ESC -> cmdid = 0x1B
        outCmd = 0x1B;
        return true;
    }
    outCmd = frame[2];
    return true;
}

void RciClient::MarkDisconnected_NoThrow() {
    SOCKET s = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        connected_.store(false, std::memory_order_release);
        s = sock_;
        sock_ = INVALID_SOCKET;

        // Khi đã coi là disconnect, pending cũng không còn ý nghĩa
        rxPending_.clear();
    }
    if (s != INVALID_SOCKET) {
        ::shutdown(s, SD_BOTH);
        ::closesocket(s);
    }
}

// Trả về body đã UNESCAPE, verify checksum (theo đúng frame reply ESC ACK ... ESC ETX chk)
ParsedReply ParseReplyAndVerify(const std::vector<uint8_t>& reply, uint8_t(*ComputeChecksumFn)(const std::vector<uint8_t>&)) {
    ParsedReply pr;
    if (reply.size() < 6) return pr;
    if (reply[0] != 0x1B) return pr;
    if (reply[1] != 0x06 && reply[1] != 0x15) return pr;

    // Find ESC ETX
    size_t esc_etx = std::string::npos;
    for (size_t i = 0; i + 1 < reply.size(); ++i) {
        if (reply[i] == 0x1B && reply[i + 1] == 0x03) { esc_etx = i; break; }
    }
    if (esc_etx == std::string::npos) return pr;

    // Determine received checksum byte (after ESC ETX)
    size_t after = esc_etx + 2;
    if (after >= reply.size()) return pr;
    uint8_t recvChk = 0;
    if (reply[after] == 0x1B) {
        if (after + 1 >= reply.size()) return pr;
        recvChk = reply[after + 1];
    }
    else {
        recvChk = reply[after];
    }

    // Unescape body bytes between index 2 .. esc_etx-1
    std::vector<uint8_t> escBody(reply.begin() + 2, reply.begin() + esc_etx);
    std::vector<uint8_t> body;
    UnescapeBodyBytes(escBody, body);
    if (body.size() < 3) return pr;

    // verify checksum over: (ACK/NAK byte) + body(unescaped) + ETX(0x03)
    std::vector<uint8_t> csArea;
    csArea.push_back(reply[1]);
    csArea.insert(csArea.end(), body.begin(), body.end());
    csArea.push_back(0x03);
    uint8_t expected = ComputeChecksumFn(csArea);
    if (expected != recvChk) return pr;

    pr.ok = true;
    pr.ack = (reply[1] == 0x06);
    pr.p_status = body[0];
    pr.c_status = body[1];
    pr.cmdid = body[2];
    pr.data.assign(body.begin() + 3, body.end());
    return pr;
}

RciClient::RciClient() : sock_(INVALID_SOCKET), connected_(false), port_(0) {
    WSADATA wsa;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (wsaResult != 0) {
        std::wcerr << L"WSAStartup failed with error: " << wsaResult << std::endl;
    }
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
// Connection Management
bool RciClient::Connect(const std::wstring& ip, unsigned short port, int timeoutMs) {
    // 1. Đảm bảo bất kỳ kết nối cũ nào cũng được đóng bên ngoài mutex
    SOCKET oldSock = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (connected_ || sock_ != INVALID_SOCKET) {
            connected_.store(false, std::memory_order_release);
            oldSock = sock_;
            sock_ = INVALID_SOCKET;
        }
    }
    if (oldSock != INVALID_SOCKET) {
        ::shutdown(oldSock, SD_BOTH);
        ::closesocket(oldSock);
    }
    // 2. Tạo socket mới
    Log(L"🔌 [Connect] Bắt đầu kết nối đến " + ip + L":" + std::to_wstring(port), 1);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        Log(L"❌ Không thể tạo socket", 2);
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    char ipUtf8[64] = {};
    WideCharToMultiByte(CP_ACP, 0, ip.c_str(), -1, ipUtf8, sizeof(ipUtf8), NULL, NULL);
    if (inet_pton(AF_INET, ipUtf8, &addr.sin_addr) <= 0) {
        Log(L"❌ Địa chỉ IP không hợp lệ", 2);
        closesocket(s);
        return false;
    }
    // 3. Đặt non-blocking tạm thời để dùng select()
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
    int res = connect(s, (sockaddr*)&addr, sizeof(addr));
    if (res == SOCKET_ERROR) {
        int err = WSAGetLastError();
        // Cho phép WSAEWOULDBLOCK / WSAEINPROGRESS / WSAEALREADY
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS && err != WSAEALREADY) {
            std::wstringstream ss;
            ss << L"❌ connect() thất bại, WSAError=" << err << L" (" << WsaErrorToString(err) << L")";
            Log(ss.str(), 2);
            closesocket(s);
            return false;
        }
    }
    // 4. Chờ socket sẵn sàng ghi (kết nối thành công)
    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(s, &wset);

    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    res = select(0, NULL, &wset, NULL, &tv);
    if (res <= 0 || !FD_ISSET(s, &wset)) {
        Log(L"⏰ Timeout kết nối", 2);
        closesocket(s);
        return false;
    }
    // 5. Kiểm tra lỗi chậm bằng SO_ERROR
    int so_err = 0;
    int optlen = sizeof(so_err);
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&so_err, &optlen) == 0) {
        if (so_err != 0) {
            std::wstringstream ss;
            ss << L"❌ Kết nối thất bại (SO_ERROR=" << so_err << L")";
            Log(ss.str(), 2);
            closesocket(s);
            return false;
        }
    }
    // 6. Trả socket về blocking
    mode = 0;
    ioctlsocket(s, FIONBIO, &mode);

    // (6.1) Apply socket options BEFORE publish
    DWORD sndTo = 2000;
    DWORD rcvTo = 2000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&sndTo, sizeof(sndTo));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&rcvTo, sizeof(rcvTo));

    // 6.2 TCP_NODELAY (phần 6 bên dưới)
    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

    // 6.3 KEEPALIVE (tuỳ chọn)
    BOOL ka = TRUE;
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char*)&ka, sizeof(ka));

    // publish connection
    {
        std::lock_guard<std::mutex> lock(mtx_);
        sock_ = s;
        host_ = ip;
        port_ = port;
        connected_.store(true, std::memory_order_release);
    }

    return true;
}

bool RciClient::Disconnect() {
    SOCKET localSock = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!connected_.load(std::memory_order_acquire) && sock_ == INVALID_SOCKET) {
            return true;
        }
        connected_.store(false, std::memory_order_release);
        localSock = sock_;
        sock_ = INVALID_SOCKET;

        rxPending_.clear(); // ✅ clear pending
    }
    if (localSock != INVALID_SOCKET) {
        ::shutdown(localSock, SD_BOTH);
        ::closesocket(localSock);
    }
    return true;
}

bool RciClient::IsConnected() const {
    return connected_.load(std::memory_order_acquire);
}
// Send / Receive
bool RciClient::SendFrame(const std::vector<uint8_t>& frame, std::vector<uint8_t>& reply, int timeoutMs)
{
    SOCKET s = INVALID_SOCKET;

    // 1) Snapshot socket + take pending
    std::vector<uint8_t> acc;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!connected_.load(std::memory_order_acquire) || sock_ == INVALID_SOCKET) return false;
        s = sock_;
        acc = rxPending_;
        rxPending_.clear();
    }
    acc.reserve(std::max<size_t>(256, acc.size() + 256));
    reply.clear();

    // 2) Apply timeouts (blocking)
    if (timeoutMs > 0) {
        DWORD rcvTo = (DWORD)timeoutMs;
        DWORD sndTo = (DWORD)min(timeoutMs, 2000);
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&rcvTo, sizeof(rcvTo));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&sndTo, sizeof(sndTo));
    }

    uint8_t cmdid = 0xFF;
    TryGetCmdIdFromFrame(frame, cmdid);
    const bool isStatusCmd = (cmdid == 0x14);

    // ✅ Chặn log hexdump cho 0x14 để đỡ loạn
    if (!isStatusCmd) {
        Log(L"➡️ TX: " + HexDump(frame), 1);
    }
    // 3) Send all
    size_t total = 0;
    while (total < frame.size()) {
        int n = send(s, (const char*)frame.data() + total, (int)(frame.size() - total), 0);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            Log(L"❌ send() failed Err=" + std::to_wstring(err), 2);
            MarkDisconnected_NoThrow();
            return false;
        }
        total += (size_t)n;
    }

    // 4) Receive one valid reply frame: must start with ESC ACK/NAK
    auto find_header = [&](const std::vector<uint8_t>& buf) -> size_t {
        for (size_t i = 0; i + 1 < buf.size(); ++i) {
            if (buf[i] == 0x1B && (buf[i + 1] == 0x06 || buf[i + 1] == 0x15)) return i;
        }
        return std::string::npos;
        };

    while (true) {
        // resync: tìm header
        size_t h = find_header(acc);
        if (h != std::string::npos && h > 0) {
            // bỏ rác trước header
            acc.erase(acc.begin(), acc.begin() + h);
        }

        // chỉ parse nếu buffer bắt đầu bằng ESC ACK/NAK
        if (acc.size() >= 2 && acc[0] == 0x1B && (acc[1] == 0x06 || acc[1] == 0x15)) {
            // tìm ESC ETX
            size_t escEtxPos = std::string::npos;
            for (size_t i = 0; i + 1 < acc.size(); ++i) {
                if (acc[i] == 0x1B && acc[i + 1] == 0x03) { escEtxPos = i; break; }
            }

            if (escEtxPos != std::string::npos) {
                size_t after = escEtxPos + 2;
                if (acc.size() > after) {
                    size_t frameEnd = 0;
                    if (acc[after] == 0x1B) {
                        if (acc.size() > after + 1) frameEnd = after + 2;
                    }
                    else {
                        frameEnd = after + 1;
                    }

                    if (frameEnd > 0) {
                        reply.assign(acc.begin(), acc.begin() + frameEnd);

                        // save leftover
                        {
                            std::lock_guard<std::mutex> lock(mtx_);
                            rxPending_.assign(acc.begin() + frameEnd, acc.end());
                        }

                        if (!isStatusCmd) {
                            Log(L"⬅️ RX: " + HexDump(reply), 1);
                        }
                        return true;
                    }
                }
            }
        }

        // need more data
        uint8_t tmp[1024];
        int n = recv(s, (char*)tmp, sizeof(tmp), 0);

        if (n == 0) {
            Log(L"❌ recv(): peer closed", 2);
            MarkDisconnected_NoThrow();
            return false;
        }
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                Log(L"⬅️ RX timeout", 1);
                // timeout: clear pending to resync
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    rxPending_.clear();
                }
                return false;
            }
            Log(L"❌ recv() failed Err=" + std::to_wstring(err), 2);
            MarkDisconnected_NoThrow();
            return false;
        }

        acc.insert(acc.end(), tmp, tmp + n);

        // tránh acc phình vô hạn nếu bị rác
        if (acc.size() > 8192) {
            // giữ lại phần cuối (chỉ để tìm header)
            acc.erase(acc.begin(), acc.end() - 2048);
        }
    }
}

bool RciClient::SendRemoteFieldDataByName(const std::string& fieldName, const std::string& value, int timeoutMs) {
    // Payload format: [numFields=1][fieldName(<=31)+0][value+0]
    std::vector<uint8_t> payload;
    payload.push_back(0x01);
    // fieldName: max 31 bytes + null (manual) :contentReference[oaicite:3]{index=3}
    AppendNullTerminated(payload, fieldName, 31);
    // (Giới hạn mềm để tránh gửi quá dài; max thực tế phụ thuộc cấu hình field trên máy)
    AppendNullTerminated(payload, value, 255);
    return SendAndWaitAck(0x9E, payload, timeoutMs);
}

bool RciClient::RequestMessagePrintCount(uint32_t& outCount, std::string& outMsgName, const std::string& msgNameOpt, int timeoutMs) {
    outCount = 0;
    outMsgName.clear();
    // payload = 16 bytes name (all 0 => current message) :contentReference[oaicite:11]{index=11}
    std::vector<uint8_t> payload = BuildMsgName16(msgNameOpt);
    std::vector<uint8_t> body;
    if (!SendAndGetBody(0x8D, payload, body, timeoutMs)) return false;
    // body = [p_status, c_status, cmdid, data...]
    // data layout (manual): count 4 bytes + msgName 16 bytes :contentReference[oaicite:12]{index=12}
    const size_t dataOffset = 3;
    if (body.size() < dataOffset + 4 + 16) return false;
    const uint8_t* p = body.data() + dataOffset;
    // LSB first thường dùng trong manual (ví dụ extended mask ghi rõ LSB first) :contentReference[oaicite:13]{index=13}
    outCount = (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
    outMsgName.assign((const char*)(p + 4), 16);
    size_t z = outMsgName.find('\0');
    if (z != std::string::npos) outMsgName.resize(z);
    return true;
}
// RCI Command Builders
uint8_t RciClient::ComputeChecksum(const vector<uint8_t>& bytes) {
    unsigned int sum = 0;
    for (auto b : bytes) sum += b;
    return (uint8_t)((0x100 - (sum & 0xFF)) & 0xFF);
}

std::vector<uint8_t> RciClient::BuildFrame(uint8_t commandId,const std::vector<uint8_t>& payload, bool useSOH, bool includeChecksum) 
{
    const uint8_t ESC = 0x1B;
    const uint8_t STX = 0x02;
    const uint8_t SOH = 0x01;
    const uint8_t ETX = 0x03;

    std::vector<uint8_t> frame;
    frame.reserve(2 + 2 + payload.size() * 2 + 4);

    // 1) Header: ESC + STX/SOH
    frame.push_back(ESC);
    frame.push_back(useSOH ? SOH : STX);

    // 2) Checksum area (UNESCAPED): [STX/SOH][cmdid][payload...][ETX]
    std::vector<uint8_t> csData;
    csData.reserve(1 + 1 + payload.size() + 1);
    csData.push_back(useSOH ? SOH : STX);
    csData.push_back(commandId);
    csData.insert(csData.end(), payload.begin(), payload.end());
    csData.push_back(ETX);

    auto push_escaped_esc_only = [&](uint8_t b) {
        // Manual behavior: only escape ESC itself (double ESC)
        if (b == ESC) {
            frame.push_back(ESC);
            frame.push_back(ESC);
        }
        else {
            frame.push_back(b);
        }
        };

    // 3) Command ID (special case: cmdid==ESC -> send ESC ESC)
    push_escaped_esc_only(commandId);

    // 4) Payload (do NOT escape <0x20; only double ESC)
    for (uint8_t b : payload) {
        push_escaped_esc_only(b);
    }

    // 5) Tail delimiter: ESC ETX (MUST be after cmd+data)
    frame.push_back(ESC);
    frame.push_back(ETX);

    // 6) Checksum byte (escape only if equals ESC)
    if (includeChecksum) {
        uint8_t chk = ComputeChecksum(csData);
        push_escaped_esc_only(chk);
    }

    return frame;
}

// High-level LINX Commands
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
    return SendAndWaitAck(0x0F, {}, 3000);

}

bool RciClient::StopJet() {
    return SendAndWaitAck(0x10, {}, 3000);
}

bool RciClient::LoadMessage(const string& name, uint16_t printCount) {
    vector<uint8_t> payload;
    std::vector<uint8_t> name16 = BuildMsgName16(name);
    payload.insert(payload.end(), name16.begin(), name16.end());

    payload.push_back(printCount & 0xFF);
    payload.push_back((printCount >> 8) & 0xFF);
    vector<uint8_t> reply;
    return SendFrame(BuildFrame(0x1E, payload), reply);
}

bool RciClient::DownloadRemoteField(const vector<uint8_t>& data) {
    vector<uint8_t> payload;
    //uint16_t len = data.size();
    size_t size = data.size();
    if (size > UINT16_MAX) {
        Log(L"Payload quá lớn", 2);
        return false;
    }
    uint16_t len = static_cast<uint16_t>(size);

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
// Extended high-level utilities for AppController
bool RciClient::SendAndWaitAck(uint8_t cmdid, const std::vector<uint8_t>& payload, int timeoutMs) {
    std::vector<uint8_t> reply;
    if (!SendFrame(BuildFrame(cmdid, payload), reply, timeoutMs)) return false;

    auto pr = ParseReplyAndVerify(reply, [](const std::vector<uint8_t>& v) { return RciClient::ComputeChecksum(v); });
    if (!pr.ok) return false;
    if (!pr.ack) return false;
    if (pr.cmdid != cmdid) return false;
    return true;
}

bool RciClient::SendAndGetBody(uint8_t cmdid, const std::vector<uint8_t>& payload, std::vector<uint8_t>& outBody, int timeoutMs) {
    outBody.clear();
    std::vector<uint8_t> reply;
    if (!SendFrame(BuildFrame(cmdid, payload), reply, timeoutMs)) return false;

    auto pr = ParseReplyAndVerify(reply, [](const std::vector<uint8_t>& v) { return RciClient::ComputeChecksum(v); });
    if (!pr.ok || !pr.ack) return false;
    if (pr.cmdid != cmdid) return false;

    // outBody format bạn đang dùng: [p_status, c_status, cmdid, data...]
    outBody.push_back(pr.p_status);
    outBody.push_back(pr.c_status);
    outBody.push_back(pr.cmdid);
    outBody.insert(outBody.end(), pr.data.begin(), pr.data.end());
    return true;
}

PrinterStatus RciClient::RequestStatusEx() {
    PrinterStatus s; // default all zero/false
    if (!IsConnected()) return s;

    std::vector<uint8_t> reply;
    // Status request: command 0x14
    if (!SendFrame(BuildFrame(0x14), reply, 500)) return s;

    auto pr = ParseReplyAndVerify(reply, [](const std::vector<uint8_t>& v) {
        return RciClient::ComputeChecksum(v);
        });

    if (!pr.ok || !pr.ack || pr.cmdid != 0x14) return s;

    // Manual: reply data for 14H includes:
    // Jet state (1), Print state (1), 32-bit Error Mask (4 bytes LSB first). :contentReference[oaicite:7]{index=7}
    if (pr.data.size() < 2 + 4) return s;

    s.jetState = pr.data[0];
    s.printState = pr.data[1];

    // error mask: LSB-first (low byte first) :contentReference[oaicite:8]{index=8}
    s.errorMask = ReadU32_LSB_First(&pr.data[2]);

    // Decode according to manual states :contentReference[oaicite:9]{index=9}
    // Jet states:
    // 00 Running, 01 Startup, 02 Shutdown, 03 Stopped, 04 Fault
    s.jetOn = (s.jetState == 0x00); // running

    // Print states:
    // 00 Printing
    // 02 Idle (ready for start print)
    // 04 Waiting (waiting trigger/delay)
    // 06 Printing/Generating Pixels (also printing)
    s.idle = (s.printState == 0x02);

    s.printing = (s.printState == 0x00 || s.printState == 0x06);

    // Optional: you may consider "ready" when jet running AND idle
    // (AppController sẽ quyết định mapping state machine)
    return s;
}
// Utility
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

bool RciClient::SetMessagePrintCount(uint32_t count, const std::string& msgNameOpt, int timeoutMs) {
    // payload = [count 4 bytes LSB-first] + [msgName 16 bytes]
    std::vector<uint8_t> payload;
    payload.reserve(4 + 16);
    payload.push_back((uint8_t)(count & 0xFF));
    payload.push_back((uint8_t)((count >> 8) & 0xFF));
    payload.push_back((uint8_t)((count >> 16) & 0xFF));
    payload.push_back((uint8_t)((count >> 24) & 0xFF));
    std::vector<uint8_t> name16 = BuildMsgName16(msgNameOpt); // 16 bytes, all 0 => current
    payload.insert(payload.end(), name16.begin(), name16.end()); return SendAndWaitAck(0x8C, payload, timeoutMs);

}