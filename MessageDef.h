#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "CommonTypes.h"
#include "CommonDefs.h"
//định nghĩa các kiểu dữ liệu Message
//Truyền dữ liệu giữa AppController ↔ UIManager
//Dùng trong message queue/event bus nội bộ
//Giúp hệ thống mở rộng và tách biệt logic (clean architecture)
//Đóng gói thông tin log gửi từ AppController tới MessageLogger.
struct LogMessage {
	std::wstring text;  //Nội dung log
    int level = 0; // 0=INFO, 1=WARNING, 2=ERROR
};
//Truyền trạng thái máy in cho UI
struct PrinterStateMessage {
	PrinterState state;          // status/jetOn/printing...
	std::wstring statusText;     // chuỗi trạng thái hiển thị
	std::wstring jobContent;     // ✅ nội dung in
	int targetCount = 0;         // ✅ số lượng cài
	int printedCount = 0;        // ✅ số lượng đã in
};
//Truyền trạng thái kết nối mạng
struct ConnectionMessage {
	bool connected; //true=nối thành công, false=mất kết nối
	std::wstring ipAddress; //Địa chỉ IP của máy in
	int port;	//Cổng kết nối
};
//Cập nhật tiến trình in ấn
struct PrintProgressMessage {	
	int current;	//Số lượng đã in
	int total;	//Tổng số lượng cần in
};
//Truyền trạng thái nút bấm trên UI
struct ButtonStateMessage {
    PrinterState state;	//Yêu cầu UI cập nhật enable/disable các nút
};