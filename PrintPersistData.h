// STRUCT LƯU DỮ LIỆU
#pragma once
#include <string>
#include <cstdint>

struct PrintPersistData {
    std::string message;
    uint32_t targetCount = 0;
    uint32_t printedCount = 0;
};