#pragma once

#include "KujataApi.h"
#include <string>

namespace KujataEngine {

// Unity風のシーン切り替えAPI(ランタイム/エディタ両方から呼べる)。
// ChangeSceneは「次フレーム境界での切り替え」を予約するだけで、その場では破棄しない。
// これにより、ComponentがUpdate中にChangeSceneを呼んでも自分自身を即解放しない。
// 実際の破棄→再構築→Initialize→(Play中なら)OnPlayStart はSceneの所有者(EditorApplication)が
// フレーム先頭で ConsumePendingSceneChange を使って実行する。

// 指定名のシーンへの切り替えを予約する(Single mode: 現シーンは全破棄して再構築)。
KUJATA_API void ChangeScene(const std::string& sceneName);

// 現在アクティブな(最後に切り替えた)シーン名。未切り替えなら空。
KUJATA_API std::string GetActiveSceneName();

// 予約中の切り替え要求を1回だけ取り出す。要求があればoutNameへ入れてtrueを返し、要求をクリアする。
// Sceneの所有者(EditorApplication)がフレーム先頭で呼ぶ。
KUJATA_API bool ConsumePendingSceneChange(std::string& outName);

} // namespace KujataEngine
