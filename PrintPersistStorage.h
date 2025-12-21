#pragma once
#include "PrintPersistData.h"
#include <optional>

class PrintPersistStorage {
public:
    static bool Save(const PrintPersistData& data);
    static std::optional<PrintPersistData> Load();
};
