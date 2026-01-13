#include "RciWarnings.h"
#include <sstream>
#include <iomanip>

static std::wstring Hex2(uint8_t v) {
    std::wstringstream ws;
    ws << std::hex << std::uppercase << std::setfill(L'0') << std::setw(2) << (int)v;
    return ws.str();
}
static std::wstring Hex8(uint32_t v) {
    std::wstringstream ws;
    ws << std::hex << std::uppercase << std::setfill(L'0') << std::setw(8) << v;
    return ws.str();
}

std::wstring JoinList(const std::vector<std::wstring>& v, const wchar_t* sep) {
    std::wstringstream ws;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) ws << sep;
        ws << v[i];
    }
    return ws.str();
}

/* =========================
   P-STATUS (printer fault codes)
   Manual Linx 8800/8900 RCI: “Summary of printer fault codes”
   ========================= */
std::wstring DecodePStatus(uint8_t p) {
    switch (p) {
    case 0x00: return L"No fault";

        // 01..13
    case 0x01: return L"Print Head Temperature";
    case 0x02: return L"EHT Trip";
    case 0x03: return L"Phase Failure";
    case 0x04: return L"Time of Flight";
    case 0x05: return L"300V Power Supply";
    case 0x06: return L"Hardware Safety Trip";
    case 0x07: return L"Ink Tank Empty";
    case 0x08: return L"Internal Spillage";

    case 0x10: return L"Solvent Tank Empty";
    case 0x11: return L"Jet Misaligned";
    case 0x12: return L"Pressure Limit";
    case 0x13: return L"Viscosity";

        // 20..36
    case 0x20: return L"Printer Over Temperature";
    case 0x21: return L"Service Module Removed";
    case 0x22: return L"Unable to fill Service Module";
    case 0x23: return L"Charge Amplifier Trip";
    case 0x24: return L"RAM Low Fault";
    case 0x25: return L"Flash Memory Low Fault";
    case 0x26: return L"Service Module Requires Replacement";
    case 0x27: return L"Valid UNIC Chip Not Found";
    case 0x28: return L"Pump Drive Failure";
    case 0x29: return L"Pressure Reading At Maximum";
    case 0x30: return L"Pressure Reading Suspect";
    case 0x31: return L"Pressure Reading At Minimum";
    case 0x32: return L"Valve Drive 1 Failure";
    case 0x33: return L"Valve Drive 2 Failure";
    case 0x34: return L"Pump Stalled";
    case 0x35: return L"Unable to Fill Solvent Buffer Tank";
    case 0x36: return L"Printhead Fault";

    case 0xFF: return L"Other Fault";

    default:
        return L"Unknown P-STATUS (0x" + Hex2(p) + L")";
    }
}

/* =========================
   C-STATUS (command status codes)
   Manual RCI: “Summary of command status codes” (Table 3-2)
   ========================= */
std::wstring DecodeCStatus(uint8_t c) {
    switch (c) {
    case 0x00: return L"No command errors";

        // Link/serial layer
    case 0x01: return L"Parity error";
    case 0x02: return L"Framing error";
    case 0x03: return L"Data overrun";
    case 0x04: return L"Serial break";
    case 0x05: return L"Receive buffer overflow";
    case 0x06: return L"Command start";
    case 0x07: return L"Command end";
    case 0x08: return L"Invalid checksum";

        // Command / state / parameter validation
    case 0x11: return L"Invalid command";
    case 0x12: return L"Jet not running";
    case 0x13: return L"Jet not idle";
    case 0x14: return L"Print not idle";
    case 0x15: return L"Message edit in progress";
    case 0x16: return L"Number of bytes in command";
    case 0x17: return L"Parameter rejected";
    case 0x18: return L"Minimum string length";
    case 0x19: return L"Maximum string length";
    case 0x1A: return L"Minimum value";
    case 0x1B: return L"Maximum value";
    case 0x1C: return L"Memory full";
    case 0x1D: return L"No character sets";
    case 0x1E: return L"No barcodes";
    case 0x1F: return L"No logos";
    case 0x20: return L"No date formats";
    case 0x21: return L"PROM-based data set specified";
    case 0x22: return L"Unknown data set";
    case 0x23: return L"No messages";
    case 0x24: return L"Unknown message";
    case 0x25: return L"Field too large";
    case 0x26: return L"Additional message overwrite";
    case 0x27: return L"Non-alphanumeric character";
    case 0x28: return L"Positive value";

        // Trigger/print specific
    case 0x29: return L"Trigger print: Photocell mode";
    case 0x2A: return L"Trigger print: Print idle";
    case 0x2B: return L"Trigger print: Already printing";
    case 0x2C: return L"Trigger print: Cover off";
    case 0x2D: return L"Print command: Jet not running";
    case 0x2E: return L"Print command: No message";

        // Jet command warnings/errors
    case 0x2F: return L"Jet command: Ink low";
    case 0x30: return L"Jet command: Solvent low";
    case 0x31: return L"Jet command: Print fail";
    case 0x32: return L"Jet command: Print in progress";
    case 0x33: return L"Jet command: Phase";
    case 0x34: return L"Jet command: Time of flight";
    case 0x35: return L"Calibrate printhead: Try later";
    case 0x36: return L"Calibrate printhead: Failed";

        // Message / raster / remote field
    case 0x37: return L"Message too large";
    case 0x38: return L"Pixel RAM overflow";
    case 0x39: return L"Invalid message format";
    case 0x3A: return L"Invalid field type";
    case 0x3B: return L"No print message loaded";
    case 0x3C: return L"Invalid print mode";
    case 0x3D: return L"Invalid failure condition";
    case 0x3E: return L"Invalid buffer divisor";
    case 0x3F: return L"No remote fields in message";
    case 0x40: return L"Number of remote characters";
    case 0x41: return L"Remote data too large";

        // NOTE: may be returned with ACK as warning
    case 0x42: return L"Remote buffer now full";
    case 0x43: return L"Remote buffer still full";

    case 0x44: return L"Field data exceeds message end";
    case 0x45: return L"Invalid remote field type";
    case 0x46: return L"Invalid while display enabled";

        // Other
    case 0x4F: return L"Too many messages specified";
    case 0x51: return L"Printer busy";
    case 0x52: return L"Unknown raster";
    case 0x53: return L"Invalid field length";
    case 0x54: return L"Duplicate name";
    case 0x55: return L"Invalid barcode linkage";
    case 0x56: return L"Data set in ROM";
    case 0x57: return L"Data set in use";
    case 0x58: return L"Invalid field height";

        // Production schedule
    case 0x59: return L"Production schedule: No message schedules";
    case 0x5A: return L"Production schedule: Too many message schedules";

    default:
        return L"Unknown C-STATUS (0x" + Hex2(c) + L")";
    }
}

/* =========================
   0x14 Warning Mask bits (Linx 88/89)
   Manual: “Printer warnings” returned as 32-bit mask
   ========================= */
std::vector<std::wstring> DecodeWarningMask(uint32_t mask) {
    std::vector<std::wstring> out;
    if (mask == 0) return out;

    auto add_if = [&](int bit, const wchar_t* text) {
        if (mask & (1u << bit)) out.emplace_back(text);
        };

    add_if(0, L"No Time of Flight");
    add_if(1, L"Shutdown Incomplete");
    add_if(2, L"Over Speed (Print Trigger)");
    add_if(3, L"Ink Low");
    add_if(4, L"Solvent Low");
    add_if(5, L"Over Speed (No Remote Data)");
    add_if(6, L"Printer Requires Scheduled Maintenance");
    add_if(7, L"Printhead Cover Off");
    add_if(8, L"Over Speed (Synchronous Data)");
    add_if(9, L"Over Speed (Line Speed)");
    add_if(10, L"Over Speed (Compensation)");
    add_if(11, L"Safety Override Active");
    add_if(12, L"Low Pressure");
    add_if(13, L"Under Speed (Line Speed)");
    add_if(14, L"Over Speed (Asynchronous Data)");

    add_if(16, L"User Data Corrupt");
    add_if(17, L"Memory Corrupt");
    add_if(18, L"No Message in Memory");

    add_if(20, L"Remote Error");
    add_if(22, L"Corrupt Program Data");
    add_if(27, L"Print Go After Schedule End");

    add_if(31, L"Extended Errors Present");

    // optional: show unknown bits for debug
    // (nếu bạn muốn debug, có thể add thêm đoạn scan bits chưa mapping)

    return out;
}
