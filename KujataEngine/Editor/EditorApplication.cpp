#include "EditorApplication.h"
#include "EditorCommandServer.h"
#include "EditorConsole.h"
#include "EditorScreenshot.h"
#include "ImGuiManager.h"
#include "AssetDatabase.h"
#include "../runtime/AssetResolver.h"
#include "../runtime/PlayState.h"
#include "../runtime/SceneManager.h"
#include "../runtime/SelectionProvider.h"
#include "../runtime/TagRegistry.h"
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
#include "../3d/GraphicsPipeline.h"
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
#include <utility>
#include <vector>

namespace KujataEngine {

namespace {

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

// ドット絵化: メインカメラの pixelSize に合わせて、Gameの描画先(3Dを描く先)を 1/pixelSize の大きさにする。
// 表示とUIは元の大きさのまま(PostProcess がぼかさずに拡大する)。このフレームでGameの描画先を使う前に呼ぶこと。
void FitGameRenderTargetToPixelSize(const Camera* camera) {
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	const int32_t pixelSize = (camera && camera->pixelSize > 1) ? camera->pixelSize : 1;
	// 割り切れないときは切り上げる(拡大したときに画面の端まで埋まるように)。
	const int32_t width = (dxCommon->GetGameOutputWidth() + pixelSize - 1) / pixelSize;
	const int32_t height = (dxCommon->GetGameOutputHeight() + pixelSize - 1) / pixelSize;
	dxCommon->ResizeGameRenderTarget(width, height);
}

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
	if (gameModule_.LoadForEditor()) {
		const GameModuleApi& api = gameModule_.GetApi();
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

#ifdef USE_IMGUI
	// CUI(Consoleの入力欄・kujata CLI・--run)の受付を始める。シーンができてから始めること(--run が最初のフレームから動くため)。
	EditorCommandServer::GetInstance().Initialize();
#endif // USE_IMGUI

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
	// エンジン側(Logger::Log)の警告・エラーを Console へ移す(別スレッドから出たものもここでメインスレッドへ)。
	EditorConsole::GetInstance()->FlushEngineLogs();
	// CUIのコマンドはここ(フレームの頭・描画の前)でだけ実行する。人間のUI操作と同じ処理を通す。
	EditorCommandServer::GetInstance().ProcessFrame();
#endif // USE_IMGUI

#ifdef USE_IMGUI
	// Editor UIはEditorApplicationを入口として更新する。
	{
		FrameProfiler::Scope profile(FrameProfiler::kEditorUI);
		ImGuiManager::GetInstance()->DrawEditor();
	}
#endif // USE_IMGUI

#ifdef USE_IMGUI
	// 自作シェーダーの .hlsl が保存されていたら読み直す(ファイルの時刻を見るだけなので、数フレームおきにする)。
	// 古いPSOを消すのでGPUの完了を待つ。描画コマンドを積む前のここで行う。
	{
		static uint32_t shaderCheckFrame = 0;
		if (++shaderCheckFrame % 30 == 0) {
			GraphicsPipeline::GetInstance()->ReloadChangedCustomShaders();
		}
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
		UpdateUIEventSystem(*currentScene_, static_cast<float>(dxCommon->GetGameOutputWidth()), static_cast<float>(dxCommon->GetGameOutputHeight()),
		                    currentScene_->GetGameViewCamera());
	}
}

void EditorApplication::Draw() {
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();

	dxCommon->PreDraw();

#ifdef USE_IMGUI
	// エディタ(ImGui)有効時: Scene/Gameを別々のオフスクリーンRTへ描き、ImGuiのImageで表示する。
	// 非表示のビュー(タブ非アクティブ/折り畳み)は描画パスをスキップして負荷を抑える。
	// CUIの view.screenshot で撮影待ちのビューは、タブが隠れていても描く。
	EditorScreenshot& screenshot = EditorScreenshot::GetInstance();
	const bool sceneVisible = IsSceneViewVisible() || screenshot.WantsView(DirectXCommon::kSceneViewIndex);
	const bool gameVisible = IsGameViewVisible() || screenshot.WantsView(DirectXCommon::kGameViewIndex);

	Camera* gameCamera = currentScene_ ? currentScene_->GetGameViewCamera() : nullptr;
	if (currentScene_ && gameCamera) {
		// --- 2画面 ---
		// 表示中のビューがあれば準備(カメラ同期・ライト・ワールド行列)は1回だけ行う。
		if (sceneVisible || gameVisible) {
			currentScene_->PrepareFrame();
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
			screenshot.RecordViewCopy(DirectXCommon::kSceneViewIndex);
		} else {
			LineRenderer::GetInstance()->Clear();
		}

		// Gameビュー(メインカメラ・オーバーレイ無し)。
		if (gameVisible) {
			FrameProfiler::Scope profile(FrameProfiler::kGameViewRender);
			FitGameRenderTargetToPixelSize(gameCamera);
			dxCommon->BeginGameRender();
			currentScene_->RenderView(gameCamera, false);
			// Collider可視化などRenderViewが積んだ線をGameビューRTへ描画(Sceneビューと同様にフラッシュ)。
			LineRenderer::GetInstance()->Render(*gameCamera);
			dxCommon->EndGameRender();
			// HDRシーンRTへフォグ+ブルーム+トーンマップを適用(GameViewWindowはこの結果を表示する)。
			// Screen Space UIはポストの影響を受けないよう、トーンマップ後のLDR RTへ重ねて描く。
			Scene* scene = currentScene_;
			scene->ApplyVolumes(gameCamera);
			// 出力は元の大きさ(ドット絵化していれば、ここでぼかさずに拡大される。UIは拡大後に元の解像度で重なる)。
			PostProcess::GetInstance()->Render(
			    DirectXCommon::kGameViewIndex, dxCommon->GetGameRenderTexture(), gameCamera, [scene](float width, float height) { scene->RenderScreenSpaceUI(width, height, false); },
			    dxCommon->GetGameOutputWidth(), dxCommon->GetGameOutputHeight());
			screenshot.RecordViewCopy(DirectXCommon::kGameViewIndex);
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
		screenshot.RecordViewCopy(DirectXCommon::kSceneViewIndex);
	} else {
		LineRenderer::GetInstance()->Clear();
	}
#else
	// エディタUI無し(ゲーム単体): メインカメラでGame用HDR RTへ描き、
	// ブルーム+トーンマップをバックバッファへ直接出力する(PSOがHDRフォーマット固定のため直描きは不可)。
	if (currentScene_) {
		FrameProfiler::Scope profile(FrameProfiler::kGameViewRender);
		currentScene_->PrepareFrame();
		Camera* camera = currentScene_->GetGameViewCamera();
		if (!camera) {
			camera = currentScene_->GetSceneViewCamera();
		}
		if (camera) {
			FitGameRenderTargetToPixelSize(camera);
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
	// CUIの view.screenshot editor: ImGuiまで描き終えたバックバッファを、画面へ出す前に写し取る。
	EditorScreenshot::GetInstance().RecordEditorCopy();
#endif // USE_IMGUI

	DirectXCommon::GetInstance()->PostDraw();
}

void EditorApplication::Finalize() {
#ifdef USE_IMGUI
	EditorCommandServer::GetInstance().Finalize();
#endif // USE_IMGUI
	if (editorMode_ == EditorMode::PrefabEdit) {
		ClosePrefabEditMode(false);
	}
	EditorSelection::GetInstance()->Clear();
	DestroyCurrentScene();
	gameModule_.UnregisterAndUnload();
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

int EditorApplication::GetExitCode() const {
#ifdef USE_IMGUI
	return EditorCommandServer::GetInstance().GetExitCode();
#else
	return 0;
#endif // USE_IMGUI
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
	if (!gameModule_.BuildNextGeneration(hotReloadDllPath)) {
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

	gameModule_.UnregisterAndUnload();

	if (!gameModule_.LoadBuiltDll(hotReloadDllPath)) {
		RestoreFallbackSceneAfterHotReloadFailure();
		return false;
	}

	// 新DLLからSceneを作り直す。SetCurrentSceneRaw内でInitializeとJSON Importをまとめて行う。
	const GameModuleApi& api = gameModule_.GetApi();
	AddConsoleLog("[HotReload] Create scene.");
	Scene* scene = api.CreateScene();
	if (!scene) {
		AddConsoleLog("[HotReload] CreateGameScene returned null.");
		gameModule_.UnregisterAndUnload();
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
	if (!gameModule_.IsLoaded()) {
		AddConsoleLog("[SceneManager] No GameModule loaded; cannot ChangeScene.");
		return;
	}
	const GameModuleApi& api = gameModule_.GetApi();
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


void EditorApplication::RestoreFallbackSceneAfterHotReloadFailure() {
	AddConsoleLog("[HotReload] Restore fallback empty Scene.");
	SetCurrentScene(std::make_unique<Scene>());
}

} // namespace KujataEngine
