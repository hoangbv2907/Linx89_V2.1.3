#include <windows.h>
#include <filesystem>
#include "Logger.h"
#include "PrintPersistStorage.h"
#include <fstream>
#include "json.hpp"
#include <ShlObj.h>   // SHGetFolderPathW
#pragma comment(lib, "Shell32.lib")
namespace nlohmann {
    using json = basic_json<>;
}
using json = nlohmann::json;

static std::wstring GetPersistDir(){
    wchar_t path[MAX_PATH] = { 0 };
    // %LOCALAPPDATA%
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path);
    std::wstring dir = std::wstring(path) + L"\\LinxController";
    CreateDirectoryW(dir.c_str(), nullptr);// Tạo folder nếu chưa tồn tại
    return dir;
}

static std::filesystem::path GetPersistPath(){
    return std::filesystem::path(GetPersistDir()) / L"last_print_data.json";
}

bool PrintPersistStorage::Save(const PrintPersistData& data){
    auto path = GetPersistPath();
    json j;
    j["message"] = data.message;
    j["target"] = data.targetCount;
    j["printed"] = data.printedCount;
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    ofs << j.dump(4);
    ofs.close();
    return true;
}

std::optional<PrintPersistData> PrintPersistStorage::Load(){
    auto path = GetPersistPath();
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return std::nullopt;

    try {
        json j;
        ifs >> j;
        PrintPersistData data;
        data.message = j.value("message", "");
        data.targetCount = j.value("target", 0);
        data.printedCount = j.value("printed", 0);
        return data;
    }
    catch (const nlohmann::json::exception& e) {
        Logger::GetInstance().Write(L"[Persist] Load FAILED (JSON parse error)", 2);
        return std::nullopt;
    }
}