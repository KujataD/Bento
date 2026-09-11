#pragma once

#include "../assets/AnimationClipAsset.h"
#include <array>
#include <string>
#include <vector>

struct ImDrawList;
struct ImVec2;

namespace KujataEngine {

class AnimatorComponent;
class GameObject;
class Scene;

/// <summary>
/// Unity風のAnimationウィンドウ。選択GameObjectのAnimatorのクリップを
/// ドープシート(トラック×キーフレーム)で編集し、スクラブプレビューする。
/// </summary>
class AnimationWindow {
public:
	void Draw(bool* pOpen);

	/// <summary>
	/// ウィンドウ非表示時にImGuiManagerから呼ばれ、プレビュー/録画状態を安全に解除します。
	/// </summary>
	void NotifyHidden();

private:
	/// <summary>
	/// "ComponentTypeName/チャンネル名" 形式のアニメーション可能チャンネル。
	/// </summary>
	struct NamedChannel {
		std::string path;
		float* value = nullptr;
	};

	void CollectChannels(GameObject& owner, const AnimatorComponent& animator, std::vector<NamedChannel>& outChannels) const;
	float* FindChannelValue(const std::vector<NamedChannel>& channels, const std::string& path) const;

	void BeginPreview(Scene& scene, AnimatorComponent& animator);
	void EndPreview(Scene& scene);
	void UpdatePreview(AnimatorComponent& animator);

	void DrawClipBar(AnimatorComponent& animator);

	/// <summary>
	/// タイムラインのズーム(ホイール)とパン(中ボタンドラッグ/Shift+ホイール)を処理します。
	/// </summary>
	void UpdateTimelineView(const ImVec2& canvasPosition, const ImVec2& canvasSize);

	/// <summary>
	/// 目盛り(可視範囲のみ・ズームに応じた間隔)とラベルを描画します。
	/// </summary>
	void DrawTimelineRuler(ImDrawList* drawList, const ImVec2& canvasPosition, const ImVec2& canvasSize);

	/// <summary>
	/// 直前のアイテムをクリップアセットのドロップターゲットにします。
	/// </summary>
	void AcceptClipDrop(AnimatorComponent& animator);
	void DrawToolbar(Scene& scene, AnimatorComponent& animator, const std::vector<NamedChannel>& channels);
	void DrawTrackList(AnimatorComponent& animator, const std::vector<NamedChannel>& channels);
	void DrawDopeSheet(AnimatorComponent& animator, const std::vector<NamedChannel>& channels);
	void DrawCurveEditor(AnimatorComponent& animator, const std::vector<NamedChannel>& channels);
	void DrawClipCreation(AnimatorComponent& animator);

	/// <summary>
	/// キー右クリックメニュー共通のイージングプリセット項目(Smooth/Linear/Flat/Constant)。
	/// </summary>
	void DrawKeyPresetMenuItems(AnimationCurve& curve, int keyIndex);

	/// <summary>
	/// 指定トラックへ現在のチャンネル値でキーを追加し、前後キーのタンジェントをSmoothに整えます。
	/// </summary>
	void AddKeyAtTime(AnimatorComponent& animator, const std::string& trackPath, const std::vector<NamedChannel>& channels, float time);

	bool previewing_ = false;
	bool previewPlaying_ = false;
	bool previewReverse_ = false; // PingPong再生の往復方向
	bool recording_ = false;

	/// <summary>ドープシート上で選択中のキー1つ分の参照。</summary>
	struct SelectedKeyRef {
		int trackIndex = 0;
		int keyIndex = 0;
	};

	/// <summary>コピーしたキー1つ分(コピー元トラックpathと、選択内の最早キーからの相対時刻)。</summary>
	struct CopiedKeyEntry {
		std::string trackPath;
		float timeOffset = 0.0f;
		AnimationKeyframe key{};
	};

	bool IsKeySelected(int trackIndex, int keyIndex) const;
	void CopySelectedKeys(const AnimationClipData& clip);
	void PasteCopiedKeys(AnimationClipData& clip);
	void DeleteSelectedKeys(AnimationClipData& clip);
	/// <summary>選択中の全キーをrequestedDeltaだけ時間移動する(未選択の隣接キーを越えないようクランプ)。</summary>
	void GroupDragSelectedKeys(AnimationClipData& clip, float requestedDelta);

	std::vector<SelectedKeyRef> selectedKeys_;
	std::vector<CopiedKeyEntry> copiedKeys_;

	// 範囲選択(ラバーバンド)の状態。
	bool boxSelectPending_ = false;
	bool boxSelecting_ = false;
	float boxSelectStartX_ = 0.0f;
	float boxSelectStartY_ = 0.0f;
	float previewTime_ = 0.0f;
	std::string previewSnapshotJson_;
	std::string previewOwnerInstanceId_;

	int selectedTrackIndex_ = -1;
	int selectedKeyIndex_ = -1;
	int viewMode_ = 0; // 0=Dopesheet, 1=Curves

	// タイムラインの表示(ズーム/スクロール)。pixelsPerSecond_<=0は「次の描画でクリップ長へ自動フィット」。
	float pixelsPerSecond_ = 0.0f;
	float timelineScroll_ = 0.0f; // 左端の時刻[s]
	std::array<char, 128> newClipNameBuffer_{};
	std::string statusMessage_;
};

} // namespace KujataEngine
