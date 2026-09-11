#include "KujataEngine.h"
#include "Editor/ImGuiManager.h"
#include "base/AudioManager.h"
#include "base/FrameProfiler.h"
#include "components/BuiltinComponents.h"
#include "runtime/AppControl.h"
#include "postprocess/PostEffectPipeline.h"
#include "postprocess/PostProcess.h"
#include "shadow/ShadowMap.h"
#include "shadow/ShadowPipeline.h"
#include <objbase.h>

namespace KujataEngine {

void Initialize(const std::wstring& title, Vector4 color, bool enableDebugLayer) {
	// COM(Component Object Model)を使うための初期化を行う。
	// ※ゲーム終了時にUnInitializeが必要。
	CoInitializeEx(0, COINIT_MULTITHREADED);

	// Loggerの初期化
	Logger::Initialize();

	// ウィンドウの生成
	WinApp* winApp = WinApp::GetInstance();
	winApp->CreateGameWindow(title);

	// DirectX の初期化
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	dxCommon->Initialize(winApp, color, WinApp::kWindowWidth, WinApp::kWindowHeight, enableDebugLayer);

#ifdef USE_IMGUI
	// ImGui の初期化
	ImGuiManager::GetInstance()->Initialize();
#endif // USE_IMGUI

	// グラフィックスパイプラインの初期化
	GraphicsPipeline::GetInstance()->Initialize();

	// ポストプロセス(ブルーム/露出/トーンマップ)の初期化。DXCを共用するためGraphicsPipelineの後。
	PostEffectPipeline::GetInstance()->Initialize();
	PostProcess::GetInstance()->Initialize();

	// シャドウマップの初期化。こちらもDXCを共用するためGraphicsPipelineの後。
	ShadowPipeline::GetInstance()->Initialize();
	ShadowMap::GetInstance()->Initialize();

	// Light初期化
	DirectionalLight::GetInstance()->Initialize();
	PointLight::GetInstance()->Initialize();
	SpotLight::GetInstance()->Initialize();

	// texture初期化
	TextureManager::GetInstance()->Initialize();

	// サウンド再生(XAudio2)初期化。失敗しても音が鳴らないだけでエンジンは続行する。
	AudioManager::GetInstance()->Initialize();

	Input::Initialize();

	Random::Initialize();

	// グローバル変数の読み込み
	GlobalVariables::GetInstance()->LoadFiles();

	// 時間管理の初期化
	Time::GetInstance()->Init();

	// Engine標準ComponentをComponentFactoryへ登録する。
	// GameModule側はゲーム固有Componentだけを登録すればよい。
	RegisterBuiltinComponents();
}

void Finalize() {
	// AudioSourceComponentを持つSceneはEditorApplication::Finalizeで破棄済み。
	// 残ったボイスとXAudio2エンジンをWindow破棄前に片付ける。
	AudioManager::GetInstance()->Finalize();

#ifdef USE_IMGUI
	// ImGuiのDX12/Win32バックエンドはWindow破棄前に終了する。
	// 先にWindowを閉じると、バックエンドが持っているHWNDやDX12リソースの後始末順が崩れやすい。
	ImGuiManager::GetInstance()->Finalize();
#endif // USE_IMGUI

	WinApp::GetInstance()->TerminateGameWindow();
}

bool Update() {

	// 前フレームの区間計測を確定し、今フレームの計測を開始する
	FrameProfiler::NewFrame();

	// ウィンドウメッセージを処理し、終了リクエストなら false を返す
	if (WinApp::GetInstance()->ProcessMessage()) {
		return false;
	}

	// ゲーム側からの終了要求(タイトルの「終了」など)。ウィンドウを壊さずここで畳む。
	if (IsQuitRequested()) {
		return false;
	}

	// 入力処理更新
	Input::Update();

	// 時間管理の更新
	Time::GetInstance()->Update();

	return true;
}

void PreDraw() {
	// バックバッファクリア＆バリア設定
	DirectXCommon::GetInstance()->PreDraw();
}

void PostDraw() {

	// プレゼント・フェンス待機・コマンドリストリセット
	DirectXCommon::GetInstance()->PostDraw();
}

} // namespace KujataEngine
