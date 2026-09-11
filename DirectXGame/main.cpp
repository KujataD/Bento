#include <KujataEngineEditor.h>
#include <base/ProjectPath.h>
#include <base/StringUtil.h>

#include "externals/nlohmann/json.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <memory>

using namespace KujataEngine;

namespace {

struct WindowSettings {
	std::wstring title = L"KujataEngine";
	Vector4 clearColor = {0.1f, 0.25f, 0.5f, 1.0f};
};

// ウィンドウのタイトルと背景色はゲームごとに違うので、エンジン側に書かずプロジェクトの設定ファイルから読む。
WindowSettings LoadWindowSettings() {
	WindowSettings settings;
	std::ifstream ifs(GetProjectDataRoot() / "ProjectSettings" / "Project.json");
	if (!ifs) {
		return settings;
	}

	nlohmann::json json = nlohmann::json::parse(ifs, nullptr, false);
	if (json.is_discarded()) {
		return settings;
	}

	if (auto title = json.find("windowTitle"); title != json.end() && title->is_string()) {
		settings.title = StringUtil::ToWString(title->get<std::string>());
	}

	if (auto color = json.find("clearColor"); color != json.end() && color->is_array() && color->size() == 4 &&
	    std::all_of(color->begin(), color->end(), [](const nlohmann::json& value) { return value.is_number(); })) {
		settings.clearColor = {(*color)[0].get<float>(), (*color)[1].get<float>(), (*color)[2].get<float>(), (*color)[3].get<float>()};
	}
	return settings;
}

} // namespace

// Windowsアプリでのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	// エンジン初期化
	const WindowSettings windowSettings = LoadWindowSettings();
	KujataEngine::Initialize(windowSettings.title, windowSettings.clearColor);

	EditorApplication* editorApplication = EditorApplication::GetInstance();
	editorApplication->Initialize();

	// ゲームループ
	while (KujataEngine::Update()) {
		editorApplication->BeginFrame();
		editorApplication->Update();
		editorApplication->Draw();
		editorApplication->EndFrame();
	}

	editorApplication->Finalize();

	// エンジンの終了処理
	KujataEngine::Finalize();

	return 0;
}
