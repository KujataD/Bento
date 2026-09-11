#pragma once

#include "../runtime/KujataApi.h"
#include "UnityAction.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace KujataEngine {

/// <summary>
/// UnityActionのInspector描画(UnityのUnityEvent欄相当)。
/// Button.onClick / Canvas.onCancel のように、複数のComponentが同じ形の欄を持つため共有する。
/// </summary>
/// <param name="label">見出し(例: "On Click ()")。</param>
/// <param name="idPrefix">
/// ImGuiのID衝突を避けるための接尾辞の種(例: "onClick")。
/// 同じObjectに複数のUnityAction欄がある場合、必ず別々の値を渡すこと。
/// </param>
KUJATA_API void DrawUnityActionInspector(const char* label, const char* idPrefix, UnityAction& action);

/// <summary>UnityActionをjson[key]へ書き出す(形式: {"calls":[{"target","component","method"}]})。</summary>
KUJATA_API void WriteUnityActionJson(nlohmann::json& json, const char* key, const UnityAction& action);

/// <summary>json[key]からUnityActionを読み込む。キーが無ければactionを空にする。</summary>
KUJATA_API void ReadUnityActionJson(const nlohmann::json& json, const char* key, UnityAction& action);

} // namespace KujataEngine
