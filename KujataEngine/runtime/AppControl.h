#pragma once

#include "KujataApi.h"

namespace KujataEngine {

// アプリケーションの終了要求。タイトル画面の「終了」ボタンなど、
// ゲーム側(GameModule)から主ループを畳みたいときに使う。
//
// ウィンドウを即破棄せず「フラグを立てて次のUpdateで抜ける」形にしているのは、
// 破棄済みのHWNDへ描画/Presentしてしまうフレームを作らないため。

/// <summary>次のKujataEngine::Update()で主ループを終了させる。</summary>
KUJATA_API void RequestQuitApplication();

/// <summary>終了が要求されているか(主ループが参照する)。</summary>
KUJATA_API bool IsQuitRequested();

} // namespace KujataEngine
