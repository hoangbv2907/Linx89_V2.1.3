#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <functional>
#include <atomic>

// ================== Parsed printer status snapshot ==================
// PrinterStatus là snapshot sau khi parse phản hồi Status (thường cmd 0x14).
// Dùng trong AppController để cập nhật PrinterModel + log delta (P/C status, warnings...).
struct PrinterStatus {
    // --- from reply header (RCI ACK/NAK + status bytes) ---
    uint8_t pStatus = 0;              // P-STATUS raw
    uint8_t cStatus = 0;              // C-STATUS raw
    std::wstring pStatusText;         // decoded text (DecodePStatus)
    std::wstring cStatusText;         // decoded text (DecodeCStatus)

    // --- from 0x14 body (status data) ---
    uint8_t jetState = 0;             // jet state raw
    uint8_t printState = 0;           // print state raw

    uint32_t errorMask = 0;           // bitmask lỗi (nếu manual có định nghĩa)
    std::vector<std::wstring> warnings; // cảnh báo đã decode (nếu có)

    // --- convenience flags (derived) ---
    bool jetOn = false;               // suy ra từ jetState / status bits
    bool printing = false;            // suy ra từ printState
    bool idle = false;                // suy ra từ printState / jet state
};

// ================== RCI client (TCP transport + protocol helpers) ==================
class RciClient {
public:
    // Callback để đẩy log/message ra ngoài (thường AppController nối vào UI logger)
    using MessageCallback = std::function<void(const std::wstring&, int)>;

    // ================== Lifecycle ==================
    RciClient();
    ~RciClient();

    // ================== Connection management ==================
    // Kết nối TCP tới printer (mặc định port 9100). timeoutMs dùng cho connect/send/recv.
    bool Connect(const std::wstring& ip, unsigned short port = 9100, int timeoutMs = 3000);

    // Ngắt kết nối và đóng socket. Trả false nếu đã disconnected hoặc có lỗi close.
    bool Disconnect();

    // Trạng thái kết nối hiện tại (cờ logic, không phải ping thực tế).
    bool IsConnected() const;

    // Mark disconnected nhanh, không throw (dùng khi detect socket error trong send/recv).
    void MarkDisconnected_NoThrow();

    // ================== Low-level send/receive ==================
    // Gửi một frame đã build sẵn và nhận reply thô.
    // Hàm này phải thread-safe (serialize bằng mtx_) vì socket chỉ nên send/recv tuần tự.
    bool SendFrame(const std::vector<uint8_t>& frame, std::vector<uint8_t>& reply, int timeoutMs = 3000);

    // Gửi cmd + payload và chờ ACK/NAK (không cần parse body).
    // Trả true nếu ACK, false nếu NAK/timeout/socket error.
    bool SendAndWaitAck(uint8_t cmdid, const std::vector<uint8_t>& payload, int timeoutMs = 3000);

    // Gửi cmd + payload và lấy BODY phản hồi (dùng cho lệnh có data trả về).
    // outBody là phần body đã tách khỏi framing (ACK/status/header... tuỳ implement).
    bool SendAndGetBody(uint8_t cmdid, const std::vector<uint8_t>& payload,
        std::vector<uint8_t>& outBody, int timeoutMs = 3000);

    // ================== High-level RCI commands ==================
    // Status: phiên bản đơn giản (chỉ gửi request). Nếu không dùng, có thể bỏ.
    bool RequestStatus();

    // Status: phiên bản đầy đủ (gửi 0x14 và parse về PrinterStatus).
    PrinterStatus RequestStatusEx();

    // Jet control
    bool StartJet();
    bool StopJet();

    // Print control
    bool StartPrint();
    bool StopPrint();

    // Message/job operations (tuỳ manual)
    bool LoadMessage(const std::string& name, uint16_t printCount = 1);
    bool DownloadRemoteField(const std::vector<uint8_t>& data);
    bool DownloadMessageData(const std::vector<uint8_t>& messageData);

    // ================== Specialized helpers used by AppController ==================
    // Send remote field data by field NAME (RCI 0x9E): fieldName + valueUtf8 (null-terminated theo manual).
    bool SendRemoteFieldDataByName(const std::string& fieldName,
        const std::string& valueUtf8,
        int timeoutMs = 3000);

    // Request message print count (RCI 0x8D):
    // outCount: số lượng đã in (hoặc target/printed tuỳ manual)
    // outMsgName: tên message trả về nếu printer trả kèm
    // msgNameOpt: nếu truyền rỗng thì lấy message hiện hành
    bool RequestMessagePrintCount(uint32_t& outCount,
        std::string& outMsgName,
        const std::string& msgNameOpt = "",
        int timeoutMs = 3000);

    // Set message print count: đặt số lượng in cho message (RCI cmd tuỳ implement).
    // Lưu ý: comment hiện tại của bạn nói "không chờ ACK"; hãy đảm bảo .cpp đúng như vậy.
    bool SetMessagePrintCount(uint32_t count, const std::string& msgNameOpt, int timeoutMs);

    // ================== Frame builders / utilities (static) ==================
    // Build frame theo chuẩn RCI:
    // - commandId: mã lệnh
    // - payload: data
    // - useSOH/includeChecksum: tuỳ lệnh / tuỳ phiên bản framing bạn đang áp dụng
    static std::vector<uint8_t> BuildFrame(uint8_t commandId,
        const std::vector<uint8_t>& payload = {},
        bool useSOH = false,
        bool includeChecksum = true);

    // Compute checksum theo rule của manual (thường là 8-bit two's complement).
    static uint8_t ComputeChecksum(const std::vector<uint8_t>& bytes);

    // Debug helper: convert reply bytes thành chuỗi hex để log.
    static std::wstring ReplyToString(const std::vector<uint8_t>& reply);

    // Set callback để đẩy log ra ngoài.
    void SetMessageCallback(MessageCallback cb) { callback_ = cb; }

private:
    // ================== Socket state ==================
    SOCKET sock_ = INVALID_SOCKET;         // TCP socket tới printer
    std::atomic_bool connected_{ false };  // cờ logic connected
    std::wstring host_;                    // IP/host đang kết nối
    unsigned short port_ = 9100;           // port đang dùng

    // Serialize send/recv để tránh nhiều thread dùng chung socket cùng lúc
    std::mutex mtx_;

    // Cache/debug flag: ACK kết quả lệnh StartPrint lần gần nhất (nếu bạn dùng để gate logic)
    bool lastStartPrintAck = false;

    // Buffer giữ bytes dư khi recv không đọc đủ frame/reply trong 1 lần (stream TCP)
    std::vector<uint8_t> rxPending_;

    // Callback log ra ngoài
    MessageCallback callback_;

    // Log nội bộ qua callback_ (type/level: info/warn/error)
    void Log(const std::wstring& msg, int type = 0);
};
