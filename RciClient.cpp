#include "RciClient.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <algorithm>

#include "RciWarnings.h"

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

// ============================================================
// Helpers (file-scope)
// ============================================================

// Append string dạng null-terminated với giới hạn byte (không tính null).
static void AppendNullTerminated(std::vector<uint8_t>& out, const std::string& s, size_t maxBytesNoNull) {
    size_t n = min(s.size(), maxBytesNoNull);
    out.insert(out.end(), s.begin(), s.begin() + n);
    out.push_back(0x00);
}

// Build buffer 16 bytes message name (null-terminated), nếu rỗng => toàn 0 (current message).
static std::vector<uint8_t> BuildMsgName16(const std::string& nameOpt) {
    std::vector<uint8_t> buf(16, 0x00);
    if (!nameOpt.empty()) {
        size_t n = std::min<size_t>(15, nameOpt.size());
        memcpy(buf.data(), nameOpt.data(), n);
        buf[n] = 0x00;
    }
    return buf;
}

// Hex dump giới hạn để log debug.
static std::wstring HexDump(const std::vector<uint8_t>& v, size_t max = 128) {
    std::wstringstream ws;
    ws << std::hex << std::setfill(L'0');
    size_t n = min(v.size(), max);
    for (size_t i = 0; i < n; ++i) ws << std::setw(2) << (int)v[i] << L' ';
    if (v.size() > n) ws << L"...";
    return ws.str();
}

// Unescape body: chỉ unescape các byte hợp lệ theo rule (b < 0x20 hoặc ESC).
static void UnescapeBodyBytes(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(in.size());

    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == 0x1B) {
            if (i + 1 >= in.size()) break;
            uint8_t b = in[i + 1];

            if (b < 0x20 || b == 0x1B) {
                out.push_back(b);
                ++i;
                continue;
            }

            // ESC + byte không hợp lệ theo rule: coi ESC là data thô
            out.push_back(0x1B);
            continue;
        }
        out.push_back(in[i]);
    }
}

// Read little-endian uint32.
static inline uint32_t ReadU32_LSB_First(const uint8_t* p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

// Parse reply (ESC ACK/NAK ... ESC ETX chk), unescape body, verify checksum.
struct ParsedReply {
    bool ok = false;
    bool ack = false; // true=ACK, false=NAK
    uint8_t p_status = 0, c_status = 0, cmdid = 0;
    std::vector<uint8_t> data;
};

// Trích cmdid từ frame TX để phục vụ log/throttle (best-effort).
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

// Convert WSA error code to readable string (for logs).
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

// Parse + verify checksum theo rule: checksum over (ACK/NAK) + body(unescaped) + ETX(0x03).
static ParsedReply ParseReplyAndVerify(const std::vector<uint8_t>& reply,uint8_t(*ComputeChecksumFn)(const std::vector<uint8_t>&))
{
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

    // verify checksum area
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

// ============================================================
// RciClient: Lifecycle
// ============================================================

/* Function: RciClient()
 * Mục đích: Khởi tạo WinSock (WSAStartup) và init state mặc định.
 */
RciClient::RciClient() : sock_(INVALID_SOCKET), connected_(false), port_(0) {
    WSADATA wsa;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (wsaResult != 0) {
        std::wcerr << L"WSAStartup failed with error: " << wsaResult << std::endl;
    }
}

/* Function: ~RciClient()
 * Mục đích: Đảm bảo disconnect socket và cleanup WinSock (WSACleanup).
 */
RciClient::~RciClient() {
    Disconnect();
    WSACleanup();
}

// ============================================================
// RciClient: Connection management
// ============================================================

/* Function: MarkDisconnected_NoThrow()
 * Mục đích: Đánh dấu disconnect ngay lập tức, đóng socket, clear rxPending_.
 * Dùng khi send/recv gặp lỗi để tránh throw và tránh dùng tiếp socket hỏng.
 */
void RciClient::MarkDisconnected_NoThrow() {
    SOCKET s = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        connected_.store(false, std::memory_order_release);
        s = sock_;
        sock_ = INVALID_SOCKET;
        rxPending_.clear();
    }
    if (s != INVALID_SOCKET) {
        ::shutdown(s, SD_BOTH);
        ::closesocket(s);
    }
}

/* Function: Connect(ip, port, timeoutMs)
 * Mục đích: Tạo kết nối TCP tới printer (non-blocking connect + select timeout),
 *          sau đó publish sock_ và set socket options (timeouts, nodelay, keepalive).
 */
bool RciClient::Connect(const std::wstring& ip, unsigned short port, int timeoutMs) {
    // 1) Close old socket (outside mutex)
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

    // 2) Create new socket
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

    // 3) Set non-blocking for connect + select
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    int res = connect(s, (sockaddr*)&addr, sizeof(addr));
    if (res == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS && err != WSAEALREADY) {
            std::wstringstream ss;
            ss << L"❌ connect() thất bại, WSAError=" << err << L" (" << WsaErrorToString(err) << L")";
            Log(ss.str(), 2);
            closesocket(s);
            return false;
        }
    }

    // 4) Wait until writable (connected)
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

    // 5) Check SO_ERROR
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

    // 6) Back to blocking
    mode = 0;
    ioctlsocket(s, FIONBIO, &mode);

    // 6.1) socket timeouts
    DWORD sndTo = 2000;
    DWORD rcvTo = 2000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&sndTo, sizeof(sndTo));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&rcvTo, sizeof(rcvTo));

    // 6.2) TCP_NODELAY
    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

    // 6.3) KEEPALIVE (optional)
    BOOL ka = TRUE;
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char*)&ka, sizeof(ka));

    // publish
    {
        std::lock_guard<std::mutex> lock(mtx_);
        sock_ = s;
        host_ = ip;
        port_ = port;
        connected_.store(true, std::memory_order_release);
        rxPending_.clear();
    }

    return true;
}

/* Function: Disconnect()
 * Mục đích: Đóng socket hiện tại, clear rxPending_, chuyển connected_ = false.
 */
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
        rxPending_.clear();
    }
    if (localSock != INVALID_SOCKET) {
        ::shutdown(localSock, SD_BOTH);
        ::closesocket(localSock);
    }
    return true;
}

/* Function: IsConnected()
 * Mục đích: Trả về cờ logic connected_ (không phải ping thực tế).
 */
bool RciClient::IsConnected() const {
    return connected_.load(std::memory_order_acquire);
}

// ============================================================
// RciClient: Low-level send/receive
// ============================================================

/* Function: SendFrame(frame, reply, timeoutMs)
 * Mục đích: Gửi raw frame (đã BuildFrame) và đọc 1 reply frame hợp lệ (ESC ACK/NAK ... ESC ETX chk).
 *          - Resync nếu có rác trong stream
 *          - Lưu phần dư vào rxPending_
 *          - timeout qua SO_RCVTIMEO/WSAETIMEDOUT
 */
bool RciClient::SendFrame(const std::vector<uint8_t>& frame, std::vector<uint8_t>& reply, int timeoutMs) {
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

    // Chặn log hexdump cho status để đỡ loạn
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
        // resync: find header
        size_t h = find_header(acc);
        if (h != std::string::npos && h > 0) {
            acc.erase(acc.begin(), acc.begin() + h);
        }

        // parse if buffer begins with ESC ACK/NAK
        if (acc.size() >= 2 && acc[0] == 0x1B && (acc[1] == 0x06 || acc[1] == 0x15)) {
            // find ESC ETX
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

        // prevent unbounded growth if garbage stream
        if (acc.size() > 8192) {
            acc.erase(acc.begin(), acc.end() - 2048);
        }
    }
}

// ============================================================
// RciClient: Frame builders / utilities (static)
// ============================================================

/* Function: ComputeChecksum(bytes)
 * Mục đích: Tính checksum 8-bit kiểu two's complement theo manual: chk = (0x100 - (sum & 0xFF)) & 0xFF.
 */
uint8_t RciClient::ComputeChecksum(const vector<uint8_t>& bytes) {
    unsigned int sum = 0;
    for (auto b : bytes) sum += b;
    return (uint8_t)((0x100 - (sum & 0xFF)) & 0xFF);
}

/* Function: BuildFrame(commandId, payload, useSOH, includeChecksum)
 * Mục đích: Build frame theo format bạn đang áp dụng:
 *          ESC + STX/SOH + [cmdid/payload (escape ESC bằng double-ESC)] + ESC ETX + [checksum (escape ESC)].
 * NOTE: Hiện implementation chỉ escape ESC (không escape <0x20). Đảm bảo khớp simulator/manual bạn đang dùng.
 */
std::vector<uint8_t> RciClient::BuildFrame(uint8_t commandId,const std::vector<uint8_t>& payload,bool useSOH,bool includeChecksum) 
{
    const uint8_t ESC = 0x1B;
    const uint8_t STX = 0x02;
    const uint8_t SOH = 0x01;
    const uint8_t ETX = 0x03;

    std::vector<uint8_t> frame;
    frame.reserve(2 + 2 + payload.size() * 2 + 4);

    // Header: ESC + STX/SOH
    frame.push_back(ESC);
    frame.push_back(useSOH ? SOH : STX);

    // checksum area (UNESCAPED): [STX/SOH][cmdid][payload...][ETX]
    std::vector<uint8_t> csData;
    csData.reserve(1 + 1 + payload.size() + 1);
    csData.push_back(useSOH ? SOH : STX);
    csData.push_back(commandId);
    csData.insert(csData.end(), payload.begin(), payload.end());
    csData.push_back(ETX);

    auto push_escaped_esc_only = [&](uint8_t b) {
        if (b == ESC) {
            frame.push_back(ESC);
            frame.push_back(ESC);
        }
        else {
            frame.push_back(b);
        }
        };

    // Command ID
    push_escaped_esc_only(commandId);

    // Payload (escape ESC only)
    for (uint8_t b : payload) {
        push_escaped_esc_only(b);
    }

    // Tail delimiter
    frame.push_back(ESC);
    frame.push_back(ETX);

    // Checksum (escape ESC only)
    if (includeChecksum) {
        uint8_t chk = ComputeChecksum(csData);
        push_escaped_esc_only(chk);
    }

    return frame;
}

/* Function: ReplyToString(reply)
 * Mục đích: Debug helper — in size + một phần bytes đầu.
 */
std::wstring RciClient::ReplyToString(const vector<uint8_t>& reply) {
    wstringstream ws;
    ws << L"[" << reply.size() << L" bytes] ";
    for (size_t i = 0; i < min(reply.size(), size_t(12)); ++i)
        ws << hex << setw(2) << setfill(L'0') << (int)reply[i] << L" ";
    return ws.str();
}

// ============================================================
// RciClient: High-level commands (simple wrappers)
// ============================================================

/* Function: RequestStatus()
 * Mục đích: Gửi lệnh STATUS (0x14) dạng đơn giản (không parse body).
 * Khuyến nghị AppController dùng RequestStatusEx() nếu cần dữ liệu.
 */
bool RciClient::RequestStatus() {
    vector<uint8_t> reply;
    auto frame = BuildFrame(0x14);
    return SendFrame(frame, reply);
}

/* Function: StartPrint()
 * Mục đích: Gửi lệnh Start Print (0x11) (simple wrapper).
 */
bool RciClient::StartPrint() {
    vector<uint8_t> reply;
    auto frame = BuildFrame(0x11);
    return SendFrame(frame, reply);
}

/* Function: StopPrint()
 * Mục đích: Gửi lệnh Stop Print (0x12) (simple wrapper).
 */
bool RciClient::StopPrint() {
    vector<uint8_t> reply;
    auto frame = BuildFrame(0x12);
    return SendFrame(frame, reply);
}

/* Function: StartJet()
 * Mục đích: Gửi lệnh Start Jet (0x0F) và chờ ACK.
 */
bool RciClient::StartJet() {
    return SendAndWaitAck(0x0F, {}, 3000);
}

/* Function: StopJet()
 * Mục đích: Gửi lệnh Stop Jet (0x10) và chờ ACK.
 */
bool RciClient::StopJet() {
    return SendAndWaitAck(0x10, {}, 3000);
}

/* Function: LoadMessage(name, printCount)
 * Mục đích: Load message theo name + set printCount (cmd 0x1E theo code hiện tại).
 */
bool RciClient::LoadMessage(const string& name, uint16_t printCount) {
    vector<uint8_t> payload;
    std::vector<uint8_t> name16 = BuildMsgName16(name);
    payload.insert(payload.end(), name16.begin(), name16.end());

    payload.push_back(printCount & 0xFF);
    payload.push_back((printCount >> 8) & 0xFF);

    vector<uint8_t> reply;
    return SendFrame(BuildFrame(0x1E, payload), reply);
}

/* Function: DownloadRemoteField(data)
 * Mục đích: Download Remote Field payload kiểu (len16 + data...) dùng cmd 0x1D theo code hiện tại.
 */
bool RciClient::DownloadRemoteField(const vector<uint8_t>& data) {
    vector<uint8_t> payload;
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

/* Function: DownloadMessageData(data)
 * Mục đích: Download Message Data (cmd 0x19 theo code hiện tại).
 */
bool RciClient::DownloadMessageData(const vector<uint8_t>& data) {
    vector<uint8_t> reply;
    return SendFrame(BuildFrame(0x19, data), reply);
}

// ============================================================
// RciClient: Extended utilities used by AppController
// ============================================================

/* Function: SendAndWaitAck(cmdid, payload, timeoutMs)
 * Mục đích: Gửi cmd + payload và verify reply:
 *          - parse reply frame
 *          - verify checksum
 *          - yêu cầu ACK và cmdid khớp
 */
bool RciClient::SendAndWaitAck(uint8_t cmdid, const std::vector<uint8_t>& payload, int timeoutMs) {
    std::vector<uint8_t> reply;
    if (!SendFrame(BuildFrame(cmdid, payload), reply, timeoutMs)) return false;

    auto pr = ParseReplyAndVerify(reply, [](const std::vector<uint8_t>& v) { return RciClient::ComputeChecksum(v); });
    if (!pr.ok) return false;
    if (!pr.ack) return false;
    if (pr.cmdid != cmdid) return false;
    return true;
}

/* Function: SendAndGetBody(cmdid, payload, outBody, timeoutMs)
 * Mục đích: Gửi cmd + payload và lấy body đã parse/unescape/verify checksum.
 * outBody format theo code hiện tại: [p_status, c_status, cmdid, data...]
 */
bool RciClient::SendAndGetBody(uint8_t cmdid,const std::vector<uint8_t>& payload,std::vector<uint8_t>& outBody,int timeoutMs) 
{
    outBody.clear();
    std::vector<uint8_t> reply;
    if (!SendFrame(BuildFrame(cmdid, payload), reply, timeoutMs)) return false;

    auto pr = ParseReplyAndVerify(reply, [](const std::vector<uint8_t>& v) { return RciClient::ComputeChecksum(v); });
    if (!pr.ok || !pr.ack) return false;
    if (pr.cmdid != cmdid) return false;

    outBody.push_back(pr.p_status);
    outBody.push_back(pr.c_status);
    outBody.push_back(pr.cmdid);
    outBody.insert(outBody.end(), pr.data.begin(), pr.data.end());
    return true;
}

/* Function: RequestStatusEx()
 * Mục đích: Gửi STATUS (0x14) và parse đầy đủ thành PrinterStatus:
 *          - p/c status + text
 *          - jetState/printState + errorMask
 *          - warnings (decode từ errorMask)
 *          - derived flags jetOn/printing/idle
 */
PrinterStatus RciClient::RequestStatusEx() {
    PrinterStatus s;
    if (!IsConnected()) return s;

    std::vector<uint8_t> reply;
    if (!SendFrame(BuildFrame(0x14), reply, 500)) return s;

    auto pr = ParseReplyAndVerify(reply, [](const std::vector<uint8_t>& v) {
        return RciClient::ComputeChecksum(v);
        });
    if (!pr.ok || !pr.ack || pr.cmdid != 0x14) return s;

    if (pr.data.size() < 2 + 4) return s;

    s.pStatus = pr.p_status;
    s.cStatus = pr.c_status;
    s.pStatusText = DecodePStatus(s.pStatus);
    s.cStatusText = DecodeCStatus(s.cStatus);

    s.jetState = pr.data[0];
    s.printState = pr.data[1];
    s.errorMask = ReadU32_LSB_First(&pr.data[2]);

    s.warnings = DecodeWarningMask(s.errorMask);

    const bool jetStopped = (s.jetState == 0x03);
    const bool jetFault = (s.jetState == 0x04);
    s.jetOn = (!jetStopped && !jetFault);

    s.idle = (s.printState == 0x02);
    s.printing = (s.printState == 0x00 || s.printState == 0x04 || s.printState == 0x06);

    return s;
}

/* Function: SendRemoteFieldDataByName(fieldName, valueUtf8, timeoutMs)
 * Mục đích: Build payload theo format 0x9E:
 *          [numFields=1][fieldName(<=31)+0][value(<=255)+0] và chờ ACK.
 */
bool RciClient::SendRemoteFieldDataByName(const std::string& fieldName,const std::string& value,int timeoutMs) 
{
    std::vector<uint8_t> payload;
    payload.push_back(0x01);
    AppendNullTerminated(payload, fieldName, 31);
    AppendNullTerminated(payload, value, 255);
    return SendAndWaitAck(0x9E, payload, timeoutMs);
}

/* Function: RequestMessagePrintCount(outCount, outMsgName, msgNameOpt, timeoutMs)
 * Mục đích: Gửi 0x8D để đọc print count + message name:
 *          payload = msgName 16 bytes (all 0 => current)
 *          response data = count(4 bytes LSB) + msgName(16 bytes)
 */
bool RciClient::RequestMessagePrintCount(uint32_t& outCount,std::string& outMsgName,const std::string& msgNameOpt,int timeoutMs)
{
    outCount = 0;
    outMsgName.clear();

    std::vector<uint8_t> payload = BuildMsgName16(msgNameOpt);
    std::vector<uint8_t> body;
    if (!SendAndGetBody(0x8D, payload, body, timeoutMs)) return false;

    const size_t dataOffset = 3;
    if (body.size() < dataOffset + 4 + 16) return false;

    const uint8_t* p = body.data() + dataOffset;
    outCount = (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);

    outMsgName.assign((const char*)(p + 4), 16);
    size_t z = outMsgName.find('\0');
    if (z != std::string::npos) outMsgName.resize(z);

    return true;
}

/* Function: SetMessagePrintCount(count, msgNameOpt, timeoutMs)
 * Mục đích: Gửi cmd 0x8C để set print count:
 *          payload = count(4 bytes LSB-first) + msgName16 (all 0 => current)
 * Hiện implementation chờ ACK (SendAndWaitAck).
 */
bool RciClient::SetMessagePrintCount(uint32_t count, const std::string& msgNameOpt, int timeoutMs) 
{
    std::vector<uint8_t> payload;
    payload.reserve(4 + 16);

    payload.push_back((uint8_t)(count & 0xFF));
    payload.push_back((uint8_t)((count >> 8) & 0xFF));
    payload.push_back((uint8_t)((count >> 16) & 0xFF));
    payload.push_back((uint8_t)((count >> 24) & 0xFF));

    std::vector<uint8_t> name16 = BuildMsgName16(msgNameOpt);
    payload.insert(payload.end(), name16.begin(), name16.end());

    return SendAndWaitAck(0x8C, payload, timeoutMs);
}

// ============================================================
// RciClient: Logging
// ============================================================

/* Function: Log(msg, type)
 * Mục đích: Đẩy log ra callback_ (nếu set) để UI/AppController hiển thị.
 */
void RciClient::Log(const std::wstring& msg, int type) {
    if (callback_) callback_(msg, type);
}
