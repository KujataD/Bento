#include "EditorApplication.h"
#include "ImGuiManager.h"
#include "AssetDatabase.h"
#include "../runtime/AssetResolver.h"
#include "../runtime/PlayState.h"
#include "../runtime/SceneManager.h"
#include "../runtime/SelectionProvider.h"
#include "../runtime/TagRegistry.h"
#include "../scene/ComponentFactory.h"
#include "../2d/UICanvasRenderer.h"
#include "../2d/UIEventSystem.h"
#include "../input/Input.h"
#include "../runtime/UIInput.h"
#include "SceneJsonExporter.h"
#include "../base/ProjectPath.h"
#include "EditorSelection.h"
#include "EditorUndoManager.h"
#include "PrefabAsset.h"
#include "SceneJsonImporter.h"
#include "../3d/Camera.h"
#include "../3d/DirectionalLight.h"
#include "../3d/LineRenderer.h"
#include "../3d/Model.h"
#include "../3d/PointLight.h"
#include "../3d/SpotLight.h"
#include "../base/DirectXCommon.h"
#include "../base/FrameProfiler.h"
#include "../postprocess/PostProcess.h"
#include "../scene/GameObject.h"
#include "../scene/IRaycastTarget.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace KujataEngine {

namespace {

std::filesystem::path FindMSBuildPath() {
	// Visual Studioのインストール先は環境ごとに異なるため、既知の候補を順に探す。
	// 見つからない場合はPATH上のMSBuild.exeへフォールバックする。
	std::vector<std::filesystem::path> candidates = {
	    "C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe",
	    "C:/Program Files/Microsoft Visual Studio/18/Professional/MSBuild/Current/Bin/MSBuild.exe",
	    "C:/Program Files/Microsoft Visual Studio/18/Enterprise/MSBuild/Current/Bin/MSBuild.exe",
	    "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe",
	    "C:/Program Files/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/MSBuild.exe",
	    "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/MSBuild/Current/Bin/MSBuild.exe",
	};

	for (const std::filesystem::path& candidate : candidates) {
		if (std::filesystem::exists(candidate)) {
			return candidate;
		}
	}

	return "MSBuild.exe";
}

std::wstring QuoteCommandArgument(const std::filesystem::path& path) {
	// CreateProcessWへ渡すコマンドライン内で、空白を含むパスを安全に扱う。
	std::wstring quoted = L"\"";
	quoted += path.wstring();
	quoted += L"\"";
	return quoted;
}

std::wstring ToMSBuildDirectoryProperty(const std::filesystem::path& path) {
	// MSBuildのDirectory系Propertyは末尾区切りがないと連結時に壊れやすい。
	// スラッシュ表記へ揃え、末尾に必ず'/'を付ける。
	std::wstring text = path.lexically_normal().generic_wstring();
	if (!text.empty() && text.back() != L'/') {
		text += L'/';
	}
	return text;
}

bool RunProcessAndWait(
    const std::filesystem::path& applicationPath,
    std::wstring commandLine,
    const std::filesystem::path& workingDirectory,
    const std::filesystem::path& outputLogPath,
    DWORD& exitCode,
    DWORD& win32Error) {
	STARTUPINFOW startupInfo{};
	startupInfo.cb = sizeof(startupInfo);

	// MSBuildの標準出力と標準エラーを同じログへ流し、Editor上のExitCodeだけでは追えない失敗原因を残す。
	HANDLE logHandle = INVALID_HANDLE_VALUE;
	HANDLE nullInputHandle = INVALID_HANDLE_VALUE;
	BOOL inheritHandles = FALSE;

	if (!outputLogPath.empty()) {
		std::error_code error;
		std::filesystem::create_directories(outputLogPath.parent_path(), error);

		SECURITY_ATTRIBUTES securityAttributes{};
		securityAttributes.nLength = sizeof(securityAttributes);
		securityAttributes.bInheritHandle = TRUE;

		// ログファイルは毎回作り直す。前回のエラーが残ると原因判断を誤るため。
		logHandle = CreateFileW(
		    outputLogPath.wstring().c_str(),
		    GENERIC_WRITE,
		    FILE_SHARE_READ,
		    &securityAttributes,
		    CREATE_ALWAYS,
		    FILE_ATTRIBUTE_NORMAL,
		    nullptr);

		if (logHandle == INVALID_HANDLE_VALUE) {
			win32Error = GetLastError();
			return false;
		}

		// MSBuildが標準入力待ちにならないよう、入力はNULへつなぐ。
		nullInputHandle = CreateFileW(
		    L"NUL",
		    GENERIC_READ,
		    FILE_SHARE_READ | FILE_SHARE_WRITE,
		    &securityAttributes,
		    OPEN_EXISTING,
		    FILE_ATTRIBUTE_NORMAL,
		    nullptr);

		startupInfo.dwFlags |= STARTF_USESTDHANDLES;
		startupInfo.hStdOutput = logHandle;
		startupInfo.hStdError = logHandle;
		if (nullInputHandle != INVALID_HANDLE_VALUE) {
			startupInfo.hStdInput = nullInputHandle;
		} else {
			startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		}
		inheritHandles = TRUE;
	}

	PROCESS_INFORMATION processInfo{};
	std::wstring applicationPathText = applicationPath.wstring();
	std::wstring workingDirectoryText = workingDirectory.wstring();

	// applicationPathをlpApplicationNameへ直接渡し、コマンドライン先頭の引用符解釈に依存しないようにする。
	BOOL created = CreateProcessW(
	    applicationPathText.c_str(),
	    commandLine.data(),
	    nullptr,
	    nullptr,
	    inheritHandles,
	    CREATE_NO_WINDOW,
	    nullptr,
	    workingDirectoryText.c_str(),
	    &startupInfo,
	    &processInfo);

	if (!created) {
		win32Error = GetLastError();
		if (nullInputHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(nullInputHandle);
		}
		if (logHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(logHandle);
		}
		return false;
	}

	// HotReloadはビルド完了後にDLLを読み直すため、MSBuildの終了まで同期的に待つ。
	WaitForSingleObject(processInfo.hProcess, INFINITE);

	BOOL gotExitCode = GetExitCodeProcess(processInfo.hProcess, &exitCode);
	if (!gotExitCode) {
		win32Error = GetLastError();
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		if (nullInputHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(nullInputHandle);
		}
		if (logHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(logHandle);
		}
		return false;
	}

	// 子プロセスとログ用ハンドルはこの関数内で必ず閉じる。
	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	if (nullInputHandle != INVALID_HANDLE_VALUE) {
		CloseHandle(nullInputHandle);
	}
	if (logHandle != INVALID_HANDLE_VALUE) {
		CloseHandle(logHandle);
	}
	return true;
}

class PrefabEditScene : public Scene {
public:
	void Initialize() override {
		camera_.Initialize();
		camera_.translation_ = {0.0f, 0.0f, -20.0f};
		camera_.rotation_ = {0.0f, 0.0f, 0.0f};
		camera_.UpdateMatrix();

		Scene::Initialize();
	}

	void Draw() override {
		camera_.UpdateMatrix();

		DirectionalLight::GetInstance()->Reset();
		PointLight::GetInstance()->Reset();
		SpotLight::GetInstance()->Reset();

		Model::PreDraw();
		DrawPrefabModels();
		Model::PostDraw();
	}

	Camera* GetEditorCamera() override {
		return &camera_;
	}

	void OnEditorComponentAdded(GameObject* gameObject, Component* component) override {
		Scene::OnEditorComponentAdded(gameObject, component);
		(void)gameObject;
		(void)component;
	}

private:
	void DrawPrefabModels() {
		UpdateWorldTransforms();

		for (const std::unique_ptr<GameObject>& gameObject : GetGameObjects()) {
			if (!gameObject || !gameObject->IsActiveInHierarchy()) {
				continue;
			}

			for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
				if (!component || !component->IsEnabled()) {
					continue;
				}

				const IRaycastTarget* raycastTarget = dynamic_cast<const IRaycastTarget*>(component.get());
				const Model* model = raycastTarget ? raycastTarget->GetRayCastModel() : nullptr;
				if (!model) {
					continue;
				}

				gameObject->UpdateWorldTransformSelfAndAncestors();
				WorldTransform& transform = gameObject->GetTransform();
				transform.UpdateMatrix(camera_);
				const_cast<Model*>(model)->Draw(transform, camera_);
			}
		}
	}

	Camera camera_;
};

} // namespace

EditorApplication* EditorApplication::GetInstance() {
	static EditorApplication instance;
	return &instance;
}

void EditorApplication::Initialize() {
	// ランタイムのアセット解決/選択問い合わせをEditor実装へ橋渡しする(依存性逆転の注入点)。
	SetAssetResolver(&AssetDatabase::GetInstance());
	SetSelectionProvider(EditorSelection::GetInstance());

	// プロジェクト共有のTag登録リストを読み込む(ProjectSettings/Tags.json)。
	TagRegistry::GetInstance().LoadFromProjectRoot(GetProjectDataRoot());

	// ポストエフェクト設定はプロジェクト共通ではなく、シーン上のVolumeComponentが持つ。
	// 毎フレームScene::PrepareFrameでVolumeStackが解決し、PostProcessへ渡される。

	editorMode_ = EditorMode::Edit;
	AddConsoleLog("Editor Mode: Edit");

	// 起動時は固定配置のGameModule.dllを読み込み、Component登録と初期Scene生成を行う。
	// 以降のHotReloadでは一時ビルドDLLへ差し替える。
	if (LoadGameModuleComponentsForEditor()) {
		const GameModuleApi& api = gameModuleLoader_.GetApi();
		if (api.CreateScene) {
			Scene* scene = api.CreateScene();
			if (scene) {
				// 起動シーンが設定されていれば、その名前でロードする(未設定なら派生の既定シーン)。
				std::string startupScene = GetStartupSceneName();
				if (!startupScene.empty()) {
					scene->SetSceneName(startupScene);
				}
				SetCurrentSceneRaw(scene, api.DestroyScene);
			}
		}
	}

	if (!currentScene_) {
		AddConsoleLog("[Editor] Fallback empty Scene.");
		SetCurrentScene(std::make_unique<Scene>());
	}

#ifndef USE_IMGUI
	// エディタUI無し(リリース/ゲーム単体)ビルドでは Start(Play)モードのみで動作させる。
	// Startボタンが無く、そのままではEditのままゲームが更新されないため、起動時に自動でPlayへ移行する。
	Start();
#endif // USE_IMGUI
}

void EditorApplication::BeginFrame() {
#ifdef USE_IMGUI
	ImGuiManager::GetInstance()->Begin();
#endif // USE_IMGUI
}

void EditorApplication::Update() {
	// フレーム先頭でシーン切り替え予約を処理する(破棄中の反復や自己解放を避けるため描画/更新の前に)。
	ProcessPendingSceneChange();

#ifdef USE_IMGUI
	// Editor UIはEditorApplicationを入口として更新する。
	{
		FrameProfiler::Scope profile(FrameProfiler::kEditorUI);
		ImGuiManager::GetInstance()->DrawEditor();
	}
#endif // USE_IMGUI

	if (ShouldUpdateGame() && currentScene_) {
		FrameProfiler::Scope profile(FrameProfiler::kSceneUpdate);
		currentScene_->Update();
	}

	// 編集中は毎フレーム、Editor用ビルボード(アイコン)を準備する。ランタイム生成された
	// service Camera等のアイコンも確実に用意する。描画パス外なのでテクスチャ生成が安全。
	if (!ShouldUpdateGame() && currentScene_) {
		currentScene_->RefreshEditorBillboards();
	}

	// UI(Canvas)のテクスチャ/フォント読み込みは描画パス外のここで行う(コマンドリスト実行を伴うため)。
	if (currentScene_) {
		PrepareSceneCanvases(*currentScene_);
	}

#ifndef USE_IMGUI
	// ランタイム(エディタUI無し)ではウィンドウのマウスをGame RT空間としてUIポインタへ渡す。
	{
		DirectXCommon* dxCommon = DirectXCommon::GetInstance();
		UIPointerState pointer;
		Vector2 mouse = Input::GetMouseClientPos();
		pointer.x = mouse.x;
		pointer.y = mouse.y;
		pointer.inside = true;
		pointer.held = Input::GetClick(0);
		pointer.pressed = Input::GetClickTrigger(0);
		pointer.released = Input::GetClickRelease(0);
		(void)dxCommon;
		SetUIPointer(pointer);
	}
#endif // USE_IMGUI

	// Play中はUIイベント(ボタン)を処理する。ポインタはGameView(編集時)/Input(実行時)がセット済み。
	if (ShouldUpdateGame() && currentScene_) {
		DirectXCommon* dxCommon = DirectXCommon::GetInstance();
		// World Space Canvasはゲームカメラからレイを飛ばして判定するため、カメラを渡す。
		UpdateUIEventSystem(*currentScene_, static_cast<float>(dxCommon->GetGameRenderWidth()), static_cast<float>(dxCommon->GetGameRenderHeight()),
		                    currentScene_->GetGameViewCamera());
	}
}

void EditorApplication::Draw() {
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();

	dxCommon->PreDraw();

#ifdef USE_IMGUI
	// エディタ(ImGui)有効時: Scene/Gameを別々のオフスクリーンRTへ描き、ImGuiのImageで表示する。
	// 非表示のビュー(タブ非アクティブ/折り畳み)は描画パスをスキップして負荷を抑える。
	const bool sceneVisible = IsSceneViewVisible();
	const bool gameVisible = IsGameViewVisible();

	Camera* gameCamera = currentScene_ ? currentScene_->GetGameViewCamera() : nullptr;
	if (currentScene_ && gameCamera) {
		// --- 2画面 ---
		// 表示中のビューがあれば準備(カメラ同期・ライト・ワールド行列)は1回だけ行う。
		if (sceneVisible || gameVisible) {
			currentScene_->PrepareFrame();
			// シャドウマップはライト視点なのでビューに依存しない。両ビューの描画前に1回だけ書く。
			currentScene_->RenderShadowPass();
		}

		// Sceneビュー(デバッグカメラ + 編集オーバーレイ)。
		if (sceneVisible) {
			FrameProfiler::Scope profile(FrameProfiler::kSceneViewRender);
			Camera* sceneCamera = currentScene_->GetSceneViewCamera();
			dxCommon->BeginSceneRender();
			currentScene_->RenderView(sceneCamera, true);
			if (sceneCamera) {
				LineRenderer::GetInstance()->Render(*sceneCamera);
			} else {
				LineRenderer::GetInstance()->Clear();
			}
			dxCommon->EndSceneRender();
			// HDRシーンRTへフォグ+ブルーム+トーンマップを適用(SceneViewWindowはこの結果を表示する)。
			// Screen Space UIはポストの影響を受けないよう、トーンマップ後のLDR RTへ重ねて描く。
			Scene* scene = currentScene_;
			// このビューのカメラ位置でVolumeを解決してから適用する(Local Volumeがビューごとに変わるため)。
			scene->ApplyVolumes(sceneCamera);
			PostProcess::GetInstance()->Render(DirectXCommon::kSceneViewIndex, dxCommon->GetSceneRenderTexture(), sceneCamera, [scene](float width, float height) { scene->RenderScreenSpaceUI(width, height, true); });
		} else {
			LineRenderer::GetInstance()->Clear();
		}

		// Gameビュー(メインカメラ・オーバーレイ無し)。
		if (gameVisible) {
			FrameProfiler::Scope profile(FrameProfiler::kGameViewRender);
			dxCommon->BeginGameRender();
			currentScene_->RenderView(gameCamera, false);
			// Collider可視化などRenderViewが積んだ線をGameビューRTへ描画(Sceneビューと同様にフラッシュ)。
			LineRenderer::GetInstance()->Render(*gameCamera);
			dxCommon->EndGameRender();
			// HDRシーンRTへフォグ+ブルーム+トーンマップを適用(GameViewWindowはこの結果を表示する)。
			// Screen Space UIはポストの影響を受けないよう、トーンマップ後のLDR RTへ重ねて描く。
			Scene* scene = currentScene_;
			scene->ApplyVolumes(gameCamera);
			PostProcess::GetInstance()->Render(DirectXCommon::kGameViewIndex, dxCommon->GetGameRenderTexture(), gameCamera, [scene](float width, float height) { scene->RenderScreenSpaceUI(width, height, false); });
		}
	} else if (currentScene_ && sceneVisible) {
		// --- 単一ビュー(Prefab編集/フォールバック): 従来のDrawをScene RTへ ---
		FrameProfiler::Scope profile(FrameProfiler::kSceneViewRender);
		dxCommon->BeginSceneRender();
		currentScene_->Draw();
		Camera* renderCamera = currentScene_->GetEditorCamera();
		if (renderCamera) {
			LineRenderer::GetInstance()->Render(*renderCamera);
		} else {
			LineRenderer::GetInstance()->Clear();
		}
		dxCommon->EndSceneRender();
		// 単一ビュー(Prefab編集等)でもトーンマップは必ず通す(HDR RTは直接表示できないため)。
		currentScene_->ApplyVolumes(renderCamera);
		PostProcess::GetInstance()->Render(DirectXCommon::kSceneViewIndex, dxCommon->GetSceneRenderTexture(), renderCamera);
	} else {
		LineRenderer::GetInstance()->Clear();
	}
#else
	// エディタUI無し(ゲーム単体): メインカメラでGame用HDR RTへ描き、
	// ブルーム+トーンマップをバックバッファへ直接出力する(PSOがHDRフォーマット固定のため直描きは不可)。
	if (currentScene_) {
		FrameProfiler::Scope profile(FrameProfiler::kGameViewRender);
		currentScene_->PrepareFrame();
		currentScene_->RenderShadowPass();
		Camera* camera = currentScene_->GetGameViewCamera();
		if (!camera) {
			camera = currentScene_->GetSceneViewCamera();
		}
		if (camera) {
			dxCommon->BeginGameRender();
			currentScene_->RenderView(camera, false);
			LineRenderer::GetInstance()->Render(*camera);
			dxCommon->EndGameRender();
			// Screen Space UIはポストの影響を受けないよう、トーンマップ後のバックバッファへ重ねて描く。
			Scene* scene = currentScene_;
			scene->ApplyVolumes(camera);
			PostProcess::GetInstance()->RenderToBackBuffer(dxCommon->GetGameRenderTexture(), camera, [scene](float width, float height) { scene->RenderScreenSpaceUI(width, height, false); });
		} else {
			LineRenderer::GetInstance()->Clear();
		}
	} else {
		LineRenderer::GetInstance()->Clear();
	}
#endif // USE_IMGUI
}

void EditorApplication::EndFrame() {
#ifdef USE_IMGUI
	{
		FrameProfiler::Scope profile(FrameProfiler::kImGuiRender);
		ImGuiManager::GetInstance()->End();
	}
#endif // USE_IMGUI

	DirectXCommon::GetInstance()->PostDraw();
}

void EditorApplication::Finalize() {
	if (editorMode_ == EditorMode::PrefabEdit) {
		ClosePrefabEditMode(false);
	}
	EditorSelection::GetInstance()->Clear();
	DestroyCurrentScene();
	UnregisterAndUnloadGameModule();
	editorMode_ = EditorMode::Edit;
}

void EditorApplication::Start() {
	if (editorMode_ == EditorMode::Play) {
		return;
	}
	if (editorMode_ == EditorMode::PrefabEdit) {
		AddConsoleLog("[Prefab] Close Prefab Edit Mode before Play.");
		return;
	}

	playModeSceneJson_.clear();
	playModeSelectedObjectInstanceId_.clear();
	if (currentScene_) {
		// Play中の変更をEditへ持ち帰らないため、開始直前のSceneを丸ごと保持する。
		playModeSceneJson_ = currentScene_->ToJson();
		GameObject* selectedObject = EditorSelection::GetInstance()->GetSelectedGameObject();
		if (selectedObject && currentScene_->FindGameObjectByInstanceId(selectedObject->GetInstanceId()) == selectedObject) {
			playModeSelectedObjectInstanceId_ = selectedObject->GetInstanceId();
		}
		currentScene_->OnPlayStart();
	}

	editorMode_ = EditorMode::Play;
	SetGamePlaying(true);

#ifdef USE_IMGUI
	ImGuiManager::GetInstance()->ClearConsoleLogs();
#endif // USE_IMGUI
	AddConsoleLog("[Editor] Start");
	AddConsoleLog("Editor Mode: Play");
}

void EditorApplication::Stop() {
	if (editorMode_ == EditorMode::Edit) {
		return;
	}
	if (editorMode_ == EditorMode::PrefabEdit) {
		return;
	}

	editorMode_ = EditorMode::Edit;
	SetGamePlaying(false);

	if (currentScene_) {
		currentScene_->OnPlayStop();
		if (!playModeSceneJson_.empty()) {
			SceneJsonImporter::ImportResult importResult = SceneJsonImporter::ApplySceneJsonString(*currentScene_, playModeSceneJson_);
			if (importResult.succeeded) {
				currentScene_->UpdateWorldTransforms();
				if (playModeSelectedObjectInstanceId_.empty()) {
					EditorSelection::GetInstance()->Clear();
				} else {
					GameObject* selectedObject = currentScene_->FindGameObjectByInstanceId(playModeSelectedObjectInstanceId_);
					if (selectedObject) {
						EditorSelection::GetInstance()->SetSelectedGameObject(selectedObject);
					} else {
						EditorSelection::GetInstance()->Clear();
					}
				}
			} else {
				AddConsoleLog("[Editor] Failed to restore Edit snapshot: " + importResult.message);
			}
		}
	}

	playModeSceneJson_.clear();
	playModeSelectedObjectInstanceId_.clear();

	AddConsoleLog("[Editor] Stop");
	AddConsoleLog("Editor Mode: Edit");
}

bool EditorApplication::IsPlaying() const {
	return editorMode_ == EditorMode::Play;
}

bool EditorApplication::IsPrefabEditing() const {
	return editorMode_ == EditorMode::PrefabEdit;
}

bool EditorApplication::OpenPrefabEditMode(const std::filesystem::path& prefabPath) {
	if (prefabPath.empty()) {
		return false;
	}
	if (editorMode_ == EditorMode::Play) {
		Stop();
	}
	if (editorMode_ == EditorMode::PrefabEdit) {
		ClosePrefabEditMode(false);
	}
	if (!currentScene_) {
		return false;
	}

	sceneBeforePrefabEdit_ = currentScene_;
	destroySceneBeforePrefabEditFunc_ = destroyCurrentSceneFunc_;
	prefabEditPath_ = prefabPath;

	prefabEditScene_ = std::make_unique<PrefabEditScene>();
	prefabEditScene_->Initialize();
	PrefabAsset::InstantiateResult instantiateResult = PrefabAsset::Instantiate(*prefabEditScene_, prefabEditPath_, false);
	if (!instantiateResult.succeeded || !instantiateResult.rootObject) {
		prefabEditScene_.reset();
		prefabEditPath_.clear();
		sceneBeforePrefabEdit_ = nullptr;
		destroySceneBeforePrefabEditFunc_ = nullptr;
		AddConsoleLog("[Prefab] Open failed: " + instantiateResult.message);
		return false;
	}

	EditorSelection::GetInstance()->SetSelectedGameObject(instantiateResult.rootObject);
	currentScene_ = prefabEditScene_.get();
	destroyCurrentSceneFunc_ = nullptr;
	editorMode_ = EditorMode::PrefabEdit;
	AddConsoleLog("[Prefab] Edit Mode: " + prefabEditPath_.string());
	return true;
}

bool EditorApplication::SavePrefabEditMode() {
	if (editorMode_ != EditorMode::PrefabEdit || !prefabEditScene_) {
		return false;
	}

	GameObject* rootObject = nullptr;
	for (const std::unique_ptr<GameObject>& gameObject : prefabEditScene_->GetGameObjects()) {
		if (gameObject && gameObject->IsRoot()) {
			rootObject = gameObject.get();
			break;
		}
	}

	if (!rootObject) {
		AddConsoleLog("[Prefab] Save failed: No root GameObject.");
		return false;
	}

	PrefabAsset::SaveResult saveResult = PrefabAsset::SaveToPath(*rootObject, prefabEditPath_);
	if (!saveResult.succeeded) {
		AddConsoleLog("[Prefab] Save failed: " + saveResult.message);
		return false;
	}

	if (sceneBeforePrefabEdit_) {
		size_t refreshedCount = PrefabAsset::RefreshInstancesFromPrefab(*sceneBeforePrefabEdit_, prefabEditPath_);
		AddConsoleLog("[Prefab] Refreshed instances: " + std::to_string(refreshedCount));
	}

	AddConsoleLog("[Prefab] Saved: " + saveResult.outputPath.string());
	return true;
}

void EditorApplication::ClosePrefabEditMode(bool saveChanges) {
	if (editorMode_ != EditorMode::PrefabEdit) {
		return;
	}

	if (saveChanges) {
		SavePrefabEditMode();
	}

	EditorSelection::GetInstance()->Clear();
	if (prefabEditScene_) {
		prefabEditScene_->Finalize();
	}
	prefabEditScene_.reset();
	prefabEditPath_.clear();

	currentScene_ = sceneBeforePrefabEdit_;
	destroyCurrentSceneFunc_ = destroySceneBeforePrefabEditFunc_;
	sceneBeforePrefabEdit_ = nullptr;
	destroySceneBeforePrefabEditFunc_ = nullptr;
	editorMode_ = EditorMode::Edit;
	AddConsoleLog("[Prefab] Back to Scene.");
}

const std::filesystem::path& EditorApplication::GetPrefabEditPath() const {
	return prefabEditPath_;
}

EditorMode EditorApplication::GetEditorMode() const {
	return editorMode_;
}

bool EditorApplication::ShouldUpdateGame() const {
	return IsPlaying();
}

void EditorApplication::SetCurrentScene(std::unique_ptr<Scene> scene) {
	Scene* rawScene = scene.release();
	SetCurrentSceneRaw(rawScene, nullptr);
}

bool EditorApplication::ReloadGameModule() {
	if (editorMode_ == EditorMode::PrefabEdit) {
		AddConsoleLog("[HotReload] Close Prefab Edit Mode before reload.");
		return false;
	}
	if (IsPlaying()) {
		// 実行中のSceneを破棄してDLLを解放するため、Reload前に必ずEditへ戻す。
		AddConsoleLog("[HotReload] Stop play mode before reload.");
		Stop();
	}

	// 固定のGameModule/bin出力を上書きせず、世代ごとの一時DLLを作る。
	// DLL/PDBがデバッガやOSに掴まれても、次の世代へ逃がせるようにするため。
	std::filesystem::path hotReloadDllPath;
	if (!BuildGameModuleForHotReload(hotReloadDllPath)) {
		return false;
	}

	// Sceneを破棄する前にJSONへ退避し、Reload後の新しいComponentインスタンスへ復元する。
	AddConsoleLog("[HotReload] Save current scene...");
	if (!SaveCurrentSceneJsonForHotReload()) {
		AddConsoleLog("[HotReload] Reload aborted because scene save failed.");
		return false;
	}

	AddConsoleLog("[HotReload] Clear editor selection.");
	EditorSelection::GetInstance()->Clear();

	// 旧DLL側で生成されたSceneとComponentを先に破棄し、DLL解放後に古い関数ポインタを触らない状態へする。
	AddConsoleLog("[HotReload] Destroy current scene.");
	DestroyCurrentScene();

	UnregisterAndUnloadGameModule();

	std::filesystem::path dllPath = hotReloadDllPath;
	std::filesystem::path copyDirectory = GetGameModuleCopyDirectory();
	AddConsoleLog("[HotReload] Copy DLL: " + dllPath.string());

	// GameModuleLoaderは読み込み前にさらに一時コピーを作る。
	// ビルド成果物とLoadLibrary中のDLLを分け、次回ビルドで上書きできるようにする。
	GameModuleLoadResult loadResult = gameModuleLoader_.Load(dllPath, copyDirectory);
	if (!loadResult.succeeded) {
		AddConsoleLog(loadResult.message);
		RestoreFallbackSceneAfterHotReloadFailure();
		return false;
	}

	AddConsoleLog("[HotReload] Load new DLL: " + loadResult.copiedDllPath.string());

	const GameModuleApi& api = gameModuleLoader_.GetApi();
	AddConsoleLog("[HotReload] Register game components.");
	api.RegisterComponents(ComponentFactory::GetInstance());

	// 新DLLからSceneを作り直す。SetCurrentSceneRaw内でInitializeとJSON Importをまとめて行う。
	AddConsoleLog("[HotReload] Create scene.");
	Scene* scene = api.CreateScene();
	if (!scene) {
		AddConsoleLog("[HotReload] CreateGameScene returned null.");
		UnregisterAndUnloadGameModule();
		RestoreFallbackSceneAfterHotReloadFailure();
		return false;
	}

	SetCurrentSceneRaw(scene, api.DestroyScene);
	AddConsoleLog("[HotReload] Completed.");
	return true;
}

Scene* EditorApplication::GetCurrentScene() const {
	return currentScene_;
}

void EditorApplication::AddConsoleLog(const std::string& message) {
#ifdef USE_IMGUI
	ImGuiManager::GetInstance()->AddConsoleLog(message);
#else
	(void)message;
#endif // USE_IMGUI
}

void EditorApplication::DestroyCurrentScene() {
	if (!currentScene_) {
		return;
	}

	currentScene_->Finalize();
	if (destroyCurrentSceneFunc_) {
		destroyCurrentSceneFunc_(currentScene_);
	} else {
		delete currentScene_;
	}

	currentScene_ = nullptr;
	destroyCurrentSceneFunc_ = nullptr;
}

void EditorApplication::SetCurrentSceneRaw(Scene* scene, GameModuleApi::DestroySceneFunc destroySceneFunc) {
	// Scene差し替え時は選択中Objectが古いSceneを指さないよう、必ず選択を解除する。
	EditorSelection::GetInstance()->Clear();
	EditorUndoManager::GetInstance()->Clear();
	playModeSceneJson_.clear();
	playModeSelectedObjectInstanceId_.clear();
	DestroyCurrentScene();

	currentScene_ = scene;
	destroyCurrentSceneFunc_ = destroySceneFunc;
	InitializeCurrentSceneAndImportJson();
}

std::string EditorApplication::GetStartupSceneName() const {
	std::filesystem::path file = GetProjectDataRoot() / "ProjectSettings" / "StartupScene.txt";
	std::ifstream ifs(file);
	if (!ifs) {
		return "";
	}
	std::string sceneName;
	std::getline(ifs, sceneName);
	return sceneName;
}

void EditorApplication::SetStartupSceneName(const std::string& sceneName) {
	std::filesystem::path dir = GetProjectDataRoot() / "ProjectSettings";
	std::error_code errorCode;
	std::filesystem::create_directories(dir, errorCode);
	std::ofstream ofs(dir / "StartupScene.txt", std::ios::trunc);
	if (ofs) {
		ofs << sceneName;
	}
	AddConsoleLog("[SceneManager] Startup scene set: " + sceneName);
}

void EditorApplication::ProcessPendingSceneChange() {
	std::string targetName;
	if (!ConsumePendingSceneChange(targetName)) {
		return; // 予約なし
	}

	// Prefab編集中はシーンポインタを別物に差し替えているため切り替えない。
	if (editorMode_ == EditorMode::PrefabEdit) {
		AddConsoleLog("[SceneManager] Ignored ChangeScene during Prefab Edit.");
		return;
	}

	// DLL由来のCreateScene/DestroySceneが無い(GameModule未ロード)場合は安全に無視する。
	if (!gameModuleLoader_.IsLoaded()) {
		AddConsoleLog("[SceneManager] No GameModule loaded; cannot ChangeScene.");
		return;
	}
	const GameModuleApi& api = gameModuleLoader_.GetApi();
	if (!api.CreateScene) {
		AddConsoleLog("[SceneManager] CreateScene unavailable.");
		return;
	}

	// 破棄でPlay状態フラグが変わる前に、実行中かどうかを控えておく。
	const bool wasPlaying = IsPlaying();

	Scene* newScene = api.CreateScene();
	if (!newScene) {
		AddConsoleLog("[SceneManager] CreateScene returned null.");
		return;
	}

	// Initialize/Importよりも前に名前を設定する(GetSceneNameがロードパスを決めるため)。
	newScene->SetSceneName(targetName);
	// 旧Sceneは捕捉済みのdestroyCurrentSceneFunc_で破棄され、新Sceneは Initialize()+ImportScene(name) される。
	SetCurrentSceneRaw(newScene, api.DestroyScene);

	// 実行中に切り替えた場合のみ、Initializeの後にOnPlayStartを発火する(順序保証)。
	if (wasPlaying && currentScene_) {
		currentScene_->OnPlayStart();
	}

	AddConsoleLog("[SceneManager] Changed scene: " + targetName);
}

void EditorApplication::InitializeCurrentSceneAndImportJson() {
	if (!currentScene_) {
		return;
	}

	// Scene標準Objectを先に作り、その後保存済みJSONを重ねてEditor状態を復元する。
	currentScene_->Initialize();
	SceneJsonImporter::ImportResult importResult = SceneJsonImporter::ImportScene(*currentScene_, GetProjectDataRoot());
	if (importResult.imported) {
		if (importResult.succeeded) {
			AddConsoleLog("[Editor] Scene JSON imported: " + importResult.sourceDirectory.string());
		} else {
			AddConsoleLog("[Editor] Scene JSON import failed: " + importResult.message);
		}
	}
	EditorUndoManager::GetInstance()->Capture(*currentScene_, "Initial");
}

bool EditorApplication::BuildGameModuleForHotReload(std::filesystem::path& outDllPath) {
	outDllPath.clear();

	// GameModuleはEditor本体と別プロジェクトとしてビルドする。
	// Editor側はDLLのC APIだけを通してScene/Componentを受け取る。
	std::filesystem::path gameModuleProjectPath = GetGameModuleProjectPath();
	if (!std::filesystem::exists(gameModuleProjectPath)) {
		AddConsoleLog("[HotReload] GameModule project was not found: " + gameModuleProjectPath.string());
		return false;
	}

	std::filesystem::path solutionDirectory = GetEngineRoot().parent_path();
	std::wstring solutionDirectoryText = ToMSBuildDirectoryProperty(solutionDirectory);
	if (solutionDirectoryText.empty()) {
		AddConsoleLog("[HotReload] Solution directory was not found.");
		return false;
	}

	// プロセスIDとTickを含めた世代名にし、Reloadのたびに出力先を変える。
	// 固定DLL/PDBを上書きしないことが、このHotReload方式の要点。
	std::ostringstream generationName;
	generationName << "pid" << GetCurrentProcessId() << "_tick" << GetTickCount64();

	std::filesystem::path buildDirectory = GetGameModuleHotReloadBuildRoot() / generationName.str();
	std::filesystem::path buildOutputDirectory = buildDirectory / "bin";
	std::filesystem::path buildIntermediateDirectory = buildDirectory / "obj";

	// OutDirとIntDirを両方分離し、DLL/PDB/ILK/OBJが前世代や固定出力と衝突しないようにする。
	std::error_code error;
	std::filesystem::create_directories(buildOutputDirectory, error);
	if (error) {
		AddConsoleLog("[HotReload] Failed to create build output directory: " + error.message());
		return false;
	}
	std::filesystem::create_directories(buildIntermediateDirectory, error);
	if (error) {
		AddConsoleLog("[HotReload] Failed to create build intermediate directory: " + error.message());
		return false;
	}

	std::wstring outDirText = ToMSBuildDirectoryProperty(buildOutputDirectory);
	std::wstring intDirText = ToMSBuildDirectoryProperty(buildIntermediateDirectory);

#ifdef _DEBUG
	const wchar_t* configuration = L"Debug";
#else
	const wchar_t* configuration = L"Release";
#endif

	std::filesystem::path msbuildPath = FindMSBuildPath();
	std::wostringstream commandLine;
	// SolutionDirはGameModule単体ビルドでは自動設定がずれるため明示する。
	// OutDir/IntDirは世代ディレクトリへ向け、Load中のDLLを上書きしない。
	commandLine << QuoteCommandArgument(msbuildPath) << L" " << QuoteCommandArgument(gameModuleProjectPath)
	            << L" /p:Configuration=" << configuration << L" /p:Platform=x64"
	            << L" /p:SolutionDir=" << solutionDirectoryText
	            << L" /p:OutDir=" << outDirText
	            << L" /p:IntDir=" << intDirText
	            << L" /m /nologo";

	AddConsoleLog("[HotReload] Build GameModule to temp directory...");
	AddConsoleLog("[HotReload] Build output: " + buildOutputDirectory.string());
	std::filesystem::path buildLogPath = buildDirectory / "GameModuleBuild.log";
	AddConsoleLog("[HotReload] MSBuild log: " + buildLogPath.string());

	DWORD exitCode = 0;
	DWORD win32Error = 0;
	std::filesystem::path workingDirectory = solutionDirectory;
	// 失敗時はMSBuildの詳細ログをConsoleから辿れるように、必ずファイルへ保存する。
	if (!RunProcessAndWait(msbuildPath, commandLine.str(), workingDirectory, buildLogPath, exitCode, win32Error)) {
		AddConsoleLog("[HotReload] Failed to launch MSBuild. Win32Error=" + std::to_string(win32Error));
		return false;
	}

	if (exitCode != 0) {
		AddConsoleLog("[HotReload] GameModule build failed. ExitCode=" + std::to_string(exitCode));
		AddConsoleLog("[HotReload] See build log: " + buildLogPath.string());
		return false;
	}

	std::filesystem::path builtDllPath = buildOutputDirectory / "GameModule.dll";
	// MSBuildが成功を返しても、期待したDLLがない場合はLoadLibrary前に止める。
	if (!std::filesystem::exists(builtDllPath)) {
		AddConsoleLog("[HotReload] Built GameModule DLL was not found: " + builtDllPath.string());
		return false;
	}

	outDllPath = builtDllPath;
	AddConsoleLog("[HotReload] GameModule build succeeded.");
	return true;
}

bool EditorApplication::LoadGameModuleComponentsForEditor() {
	if (gameModuleLoader_.IsLoaded()) {
		return true;
	}

	// 起動直後はまだHotReload用の一時ビルドが存在しないため、標準配置のDLLを読み込む。
	std::filesystem::path dllPath = GetGameModuleDllPath();
	std::filesystem::path copyDirectory = GetGameModuleCopyDirectory();
	AddConsoleLog("[GameModule] Load DLL: " + dllPath.string());

	GameModuleLoadResult loadResult = gameModuleLoader_.Load(dllPath, copyDirectory);
	if (!loadResult.succeeded) {
		AddConsoleLog("[GameModule] Failed to load: " + loadResult.message);
		return false;
	}

	AddConsoleLog("[GameModule] Loaded: " + loadResult.copiedDllPath.string());

	const GameModuleApi& api = gameModuleLoader_.GetApi();
	if (!api.RegisterComponents) {
		AddConsoleLog("[GameModule] RegisterGameComponents is null.");
		UnregisterAndUnloadGameModule();
		return false;
	}

	AddConsoleLog("[GameModule] Register game components.");
	api.RegisterComponents(ComponentFactory::GetInstance());
	return true;
}

bool EditorApplication::SaveCurrentSceneJsonForHotReload() {
	if (!currentScene_) {
		AddConsoleLog("[HotReload] No current scene to save.");
		return true;
	}

	SceneJsonExporter::ExportResult exportResult = SceneJsonExporter::ExportScene(*currentScene_, GetProjectDataRoot());
	if (exportResult.succeeded) {
		AddConsoleLog("[HotReload] Scene JSON exported: " + exportResult.outputDirectory.string());
		return true;
	}

	AddConsoleLog("[HotReload] Scene JSON export failed: " + exportResult.message);
	return false;
}

void EditorApplication::UnregisterAndUnloadGameModule() {
	ComponentFactory& factory = ComponentFactory::GetInstance();
	if (gameModuleLoader_.IsLoaded()) {
		// DLL側に登録解除の機会を渡してから、Engine側でもmoduleName単位で掃除する。
		AddConsoleLog("[HotReload] Unregister game components.");
		const GameModuleApi& api = gameModuleLoader_.GetApi();
		if (api.UnregisterComponents) {
			api.UnregisterComponents(factory);
		}
	}

	factory.UnregisterByModule("GameModule");

	if (gameModuleLoader_.IsLoaded()) {
		AddConsoleLog("[HotReload] Unload old DLL.");
		gameModuleLoader_.Unload();
	}
}

std::filesystem::path EditorApplication::GetGameModuleProjectPath() const {
	return GetActiveProjectRoot() / "GameModule" / "GameModule.vcxproj";
}

std::filesystem::path EditorApplication::GetGameModuleDllPath() const {
	// 配布パッケージ(exeと同じフォルダにGameModule.dllをコピー済み)を最優先で探す。
	// これが無い場合のみ、開発ツリー(.sln探索)側のHotReload用配置にフォールバックする。
	std::filesystem::path besideExe = GetExecutableDirectory() / "GameModule.dll";
	std::error_code error;
	if (std::filesystem::exists(besideExe, error)) {
		return besideExe;
	}

	// GameModule.dllはビルド構成(Debug/Release)ごとに別フォルダへ出力される。
	// exeと同じ構成のDLLを読み込まないとstd::string等のABIが食い違いクラッシュするため、
	// exe自身の構成に対応するサブフォルダを選ぶ(GameModule.vcxprojのOutDirと一致させること)。
#ifdef _DEBUG
	const char* configuration = "Debug";
#else
	const char* configuration = "Release";
#endif
	std::filesystem::path root = GetActiveProjectRoot();
	return root / "GameModule" / "bin" / configuration / "GameModule.dll";
}

std::filesystem::path EditorApplication::GetGameModuleHotReloadBuildRoot() const {
	return GetGameModuleCopyDirectory() / "Build";
}

std::filesystem::path EditorApplication::GetGameModuleCopyDirectory() const {
#ifdef USE_IMGUI
	return GetActiveProjectRoot() / "Temp" / "HotReload";
#else
	// エディタUI無し(ゲーム単体)ビルドではHotReloadしないため、コピー先を持たない。
	// GameModuleLoaderは空のcopyDirectoryならDLLを直接読み込み、Tempフォルダを作らない。
	return {};
#endif // USE_IMGUI
}

void EditorApplication::RestoreFallbackSceneAfterHotReloadFailure() {
	AddConsoleLog("[HotReload] Restore fallback empty Scene.");
	SetCurrentScene(std::make_unique<Scene>());
}

} // namespace KujataEngine
