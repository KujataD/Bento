#pragma once

#include "KujataApi.h"

namespace KujataEngine {

class IAssetResolver;
class ISelectionProvider;

// エンジンのランタイムサービスを集約する注入コンテキスト(DIPの土台)。
// Editor/アプリが起動時に各サービスを設定し、ランタイムはここ経由で参照する。
// 個々のアクセサ(IsGamePlaying / GetAssetResolver / GetSelectionProvider)は
// このコンテキストの薄いラッパー。将来サービスを増やす時はここへフィールドを追加する。
struct EngineContext {
	// Play(実行)中かどうか。
	bool gamePlaying = false;
	// Sceneビュー(ウィンドウ)がフォーカスされているか。デバッグカメラ操作の入力ルーティングに使う。
	bool sceneViewFocused = false;
	// Scene/Gameビューが表示されているか(タブ非アクティブ/折り畳み時はfalse)。
	// 非表示のビューは描画パスをスキップして負荷を抑える。既定はtrue(初回フレーム/非Editor時に描く)。
	bool sceneViewVisible = true;
	bool gameViewVisible = true;
	// SceneビューのUI編集モードが有効か。有効な間はSceneビューにCanvasのUIを常に描画する。
	bool sceneViewUIEditMode = false;
	// アセットID↔パスの解決(未設定時は各アクセサがフォールバックを使う)。
	IAssetResolver* assetResolver = nullptr;
	// Editorの選択状態の問い合わせ。
	ISelectionProvider* selectionProvider = nullptr;
};

// プロセス全体で唯一のEngineContextを取得する。
KUJATA_API EngineContext& GetEngineContext();

} // namespace KujataEngine
