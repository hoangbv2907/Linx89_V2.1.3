#include "PrintPersistStorage.h"
#include <fstream>
#include "json.hpp"
namespace nlohmann {
    using json = basic_json<>;
}
using json = nlohmann::json;
static const char* FILE_NAME = "last_print_data.json";

bool PrintPersistStorage::Save(const PrintPersistData& data) {
    json j;
    j["message"] = data.message;
    j["target"] = data.targetCount;
    j["printed"] = data.printedCount;

    std::ofstream ofs(FILE_NAME);
    if (!ofs) return false;
    ofs << j.dump(4);
    return true;
}

std::optional<PrintPersistData> PrintPersistStorage::Load() {
    std::ifstream ifs(FILE_NAME);
    if (!ifs)
        return std::nullopt;

    try {
        json j;
        ifs >> j;   // ❗ có thể throw

        PrintPersistData data;
        data.message = j.value("message", "");
        data.targetCount = j.value("target", 0);
        data.printedCount = j.value("printed", 0);

        return data;
    }
    catch (const nlohmann::json::exception& e) {
        // ⚠️ JSON lỗi → KHÔNG CRASH
        // Có thể log nếu muốn
        // Logger::GetInstance().Write(L"JSON load error", 2);

        return std::nullopt;
    }
}
