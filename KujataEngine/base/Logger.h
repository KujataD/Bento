#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

#include "../runtime/KujataApi.h"

namespace KujataEngine {

/// <summary>
/// ログ管理クラス
/// </summary>
class KUJATA_API Logger {
public: 
    static void Initialize();
    static void Log(const std::string& message);
};

} // namespace KujataEngine