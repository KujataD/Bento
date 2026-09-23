#pragma once
#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_impl_dx12.h"
#include "../../externals/imgui/imgui_impl_win32.h"
#include "AnimationWindow.h"
#include "EditorDockSpace.h"
#include "EditorWindowVisibility.h"
#include "GameViewWindow.h"
#include "HierarchyWindow.h"
#include "InspectorWindow.h"
#include "PerformanceWindow.h"
#include "ProjectWindow.h"
#include "InputActionWindow.h"
#include "RenderingWindow.h"
#include "SceneViewWindow.h"
#include <string>
#include <utility>
#include <vector>

namespace KujataEngine {

// ImGuiバックエンド（DX12/Win32）の初期化・フレーム制御・終了と、
// Editor各ウィンドウの毎フレーム描画の統括を行う。
class ImGuiManager {
public:
	static ImGuiManager* GetInstance();

	void Initialize();

	/// <summary>
	/// フレーム開始処理（更新処理の先頭で呼ぶ）
	/// </summary>
	void Begin();

	/// <summary>
	/// フレーム終了・描画処理（描画処理の最後に呼ぶ）
	/// </summary>
	void End();

	void DrawEditor();

	void AddConsoleLog(const std::string& message);

	void ClearConsoleLogs();

	void Finalize();

	/// <summary>
	/// Windowメニューのウィンドウを開く/閉じる(CUIの window.show)。開くときは前面(ドッキング中ならそのタブ)に出す。
	/// nameはウィンドウ名(Scene / Game / Hierarchy / Inspector / Project / Console / Performance / Animation / Scenes / Rendering)。
	/// 知らない名前ならfalse。
	/// </summary>
	bool ShowWindow(const std::string& name, bool visible);

	/// <summary>各ウィンドウの名前と、開いているか。</summary>
	std::vector<std::pair<std::string, bool>> GetWindowVisibilities() const;

private:
	ImGuiManager() = default;
	~ImGuiManager() = default;
	ImGuiManager(const ImGuiManager&) = delete;
	ImGuiManager& operator=(const ImGuiManager&) = delete;

	void HandleEditorShortcuts();
	void ExportCurrentSceneJson();

	/// <summary>
	/// メニューバーの中身(File/Edit/GameObject/Window)を描画する。
	/// DockSpaceのBeginMenuBar内からコールバックとして呼ばれる。
	/// </summary>
	void DrawMainMenuBar();

	/// <summary>
	/// メニューバー下のツールバー帯(再生・編集コントロール)を描画する。
	/// DockSpaceのツールバー用childからコールバックとして呼ばれる。
	/// </summary>
	void DrawToolbar();

	// Unity風のシーン一覧・切替(ChangeScene)・新規作成を行うウィンドウ。
	void DrawSceneListWindow();

	/// <summary>
	/// ウィンドウの表示状態をimgui.iniへ読み書きするハンドラを登録する。最初のNewFrameより前に呼ぶこと。
	/// </summary>
	void RegisterWindowVisibilitySettings();

private:
	EditorDockSpace dockSpace_;
	EditorWindowVisibility windowVisibility_;
	// 最後にimgui.iniへ書いた(または読んだ)表示状態。変わったらiniを保存させる。
	EditorWindowVisibility savedWindowVisibility_;
	// io.IniFilenameはポインタを持つだけなので、文字列の実体をここで保持する。
	std::string iniFilePath_;
	AnimationWindow animationWindow_;
	HierarchyWindow hierarchyWindow_;
	InspectorWindow inspectorWindow_;
	ProjectWindow projectWindow_;
	SceneViewWindow sceneView_;
	GameViewWindow gameView_;
	PerformanceWindow performanceWindow_;
	RenderingWindow renderingWindow_;
	InputActionWindow inputActionWindow_;
};

} // namespace KujataEngine
