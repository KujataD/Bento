#pragma once

#include "KujataApi.h"
#include <cstddef>

namespace KujataEngine::InspectorUI {

// GameModule上のComponentはImGui本体を直接リンクせず、エンジン側のImGuiコンテキストでInspector UIを描画する。

KUJATA_API bool DragFloat(const char* label, float* value, float speed = 1.0f, float minValue = 0.0f, float maxValue = 0.0f, const char* format = "%.3f");
KUJATA_API bool DragFloat2(const char* label, float* values, float speed = 1.0f, float minValue = 0.0f, float maxValue = 0.0f, const char* format = "%.3f");
KUJATA_API bool DragFloat3(const char* label, float* values, float speed = 1.0f, float minValue = 0.0f, float maxValue = 0.0f, const char* format = "%.3f");
KUJATA_API bool DragInt(const char* label, int* value, float speed = 1.0f, int minValue = 0, int maxValue = 0);
KUJATA_API bool Checkbox(const char* label, bool* value);
KUJATA_API bool ColorEdit4(const char* label, float* values);
KUJATA_API bool Combo(const char* label, int* currentItem, const char* const* items, int itemCount);
KUJATA_API bool InputText(const char* label, char* buffer, std::size_t bufferSize);
KUJATA_API bool Button(const char* label);
KUJATA_API bool ButtonSized(const char* label, float width, float height);

/// <summary>
/// オブジェクト参照フィールド。currentName(未設定は"None")を表示し、HierarchyからGameObjectが
/// ドロップされたらoutDroppedObjectにそのGameObject*を入れてtrueを返す。Clearボタンでは*outClearedをtrue。
/// UnityのInspectorのオブジェクト代入欄に相当。
/// </summary>
KUJATA_API bool ObjectField(const char* label, const char* currentName, void** outDroppedObject, bool* outCleared);

/// <summary>
/// Project(エクスプローラー)からModelアセットをドロップして参照を設定するフィールド。
/// currentDisplay(未設定は"None")を表示し、ModelファイルがドロップされたらoutBufferへ
/// ドロップ元パスを書き込みtrueを返す。UnityのInspectorのMeshアサイン欄に相当。
/// </summary>
KUJATA_API bool ModelAssetField(const char* label, const char* currentDisplay, char* outBuffer, std::size_t outBufferSize);

KUJATA_API void TextUnformatted(const char* text);
KUJATA_API void TextDisabled(const char* text);
KUJATA_API void SameLine();

/// <summary>
/// ImGuiのIDスタックを積み下ろしします。
/// ImGuiはラベル文字列をウィジェットのIDに使うため、同じ構造体を複数並べる(配列風の)Inspectorでは
/// 同名ラベルがID衝突を起こす(ImGui 1.91以降はエラー表示される)。要素ごとにPushId/PopIdで囲むこと。
/// </summary>
KUJATA_API void PushId(const char* id);
KUJATA_API void PopId();

/// <summary>
/// 直前に描画したフィールドへホバー時の説明(ツールチップ)を付けます。
/// textがnullptrか空なら何もしません。日本語も表示できます
/// (ImGuiManagerがGetGlyphRangesJapanese付きで日本語フォントをマージ済み)。
/// </summary>
KUJATA_API void ItemTooltip(const char* text);

/// <summary>
/// 直前に描画したフィールドをアニメーション録画に接続します。
/// - 録画中に値が変更されたら現在のプレイヘッドへ自動キー登録
/// - AnimationWindowがクリップを開いている間、右クリックで"Add Keyframe"を提供
/// countは成分数(float=1, Vector3=3, Vector4=4)。channelKeyはJSONキー名(成分suffixは自動付与)。
/// </summary>
KUJATA_API void AnimationFieldHook(const char* channelKey, const float* values, int count, bool changedByWidget);

} // namespace KujataEngine::InspectorUI
