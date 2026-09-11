#pragma once

#include "KujataApi.h"

namespace KujataEngine {

// ゲームがPlay(実行)中かどうかを表す、ランタイム側が所有する状態。
// EditorがStart/Stopで設定し、ランタイム(Scene/Component/ゲームロジック)が参照する。
// Editorへの逆依存を作らないよう、状態そのものはランタイム層が持つ。
KUJATA_API bool IsGamePlaying();

// Play/Edit状態を設定する(Editor側が呼ぶ)。
KUJATA_API void SetGamePlaying(bool playing);

// Sceneビュー(ウィンドウ)がフォーカスされているか。Editorが毎フレーム設定する。
// これがtrueの時だけデバッグカメラをキーボード/マウスで操作する(プレイ中も含む)。
KUJATA_API bool IsSceneViewFocused();

// Sceneビューのフォーカス状態を設定する(Editor側が呼ぶ)。
KUJATA_API void SetSceneViewFocused(bool focused);

// Scene/Gameビューが表示中か。非表示なら描画パスをスキップして負荷を抑える。Editorが毎フレーム設定する。
KUJATA_API bool IsSceneViewVisible();
KUJATA_API void SetSceneViewVisible(bool visible);
KUJATA_API bool IsGameViewVisible();
KUJATA_API void SetGameViewVisible(bool visible);

// SceneビューのUI編集モードが有効か。有効な間はSceneビューにCanvasのUIを常に描画する。Editorが設定する。
KUJATA_API bool IsSceneViewUIEditMode();
KUJATA_API void SetSceneViewUIEditMode(bool enabled);

} // namespace KujataEngine
