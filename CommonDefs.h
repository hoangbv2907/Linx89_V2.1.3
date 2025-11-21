#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

// Control IDs
#define IDC_TOGGLE     1001
#define IDC_BTN_UPLOAD 1002
#define IDC_BTN_START  1003
#define IDC_BTN_PRINT  1004
#define IDC_BTN_STOP   1005
#define IDC_BTN_CLEAR  1006
#define IDC_BTN_SET    1007
#define IDC_CURRENT_COUNT 1008

// Custom messages for thread-safe UI updates
#define WM_APP_LOG             (WM_APP + 1)
#define WM_APP_PRINTER_UPDATE  (WM_APP + 2) 
#define WM_APP_CONNECTION_UPDATE (WM_APP + 3)
#define WM_APP_PRINT_PROGRESS  (WM_APP + 4)
#define WM_APP_BUTTON_STATE    (WM_APP + 5)

// Forward declarations
struct PrinterState;
enum class PrinterStatusType;