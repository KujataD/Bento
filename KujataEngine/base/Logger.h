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

    /// <summary>
    /// Logで出したメッセージを受け取る関数を登録する(エディタがログファイルへ流すため)。nullptrで解除。
    /// Releaseでもメッセージは渡す(ファイル出力はDebugのみ)。
    /// </summary>
    static void SetListener(void (*listener)(const std::string& message));
};

} // namespace KujataEngine