#pragma once
#include "DanData.hpp"
#include <string>

struct DanLoader {
    static DanData load(const std::string& rootPath);
};
