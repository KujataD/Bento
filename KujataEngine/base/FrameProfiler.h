#pragma once
#include <cstdint>

#include "../runtime/KujataApi.h"

namespace KujataEngine {

// フレーム内の区間CPU時間を計測する簡易プロファイラ。
// Begin/Endで囲んだ区間の経過時間をフレームごとに集計し、
// PerformanceWindowでの表示と定期ログ出力(既定300フレームごと)に使う。
class KUJATA_API FrameProfiler {
public:
	enum Section {
		kSceneUpdate,     // Scene::Update全体(ゲームロジック+物理+コリジョン)
		kCollision,       // うちUpdateCollisionsのみ(kSceneUpdateに内包される)
		kEditorUI,        // ImGuiエディタUI構築(DrawEditor)
		kSceneViewRender, // Sceneビュー描画コマンド積み
		kGameViewRender,  // Gameビュー描画コマンド積み
		kImGuiRender,     // ImGui描画(ImGuiManager::End)
		kPresentWait,     // Present+フェンス待ち(GPU待ちストールがここに出る)
		kSectionCount
	};

	// フレーム先頭で呼ぶ。前フレームの集計を確定し、今フレームの計測を開始する。
	static void NewFrame();

	static void Begin(Section section);
	static void End(Section section);

	// 前フレームの実測値(ms)。
	static float GetLastMs(Section section);
	// 表示用の平滑化値(ms)。
	static float GetSmoothedMs(Section section);
	// 前フレームのフレーム全体時間(ms)と平滑化FPS。
	static float GetFrameMs();
	static float GetSmoothedFps();

	static const char* GetName(Section section);

	// Begin/Endを対で書くためのスコープガード。
	class Scope {
	public:
		explicit Scope(Section section) : section_(section) { Begin(section_); }
		~Scope() { End(section_); }
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		Section section_;
	};
};

} // namespace KujataEngine
