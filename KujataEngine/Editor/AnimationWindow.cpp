#include "AnimationWindow.h"

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#include "../assets/AnimationClipAsset.h"
#include "../base/ProjectPath.h"
#include "../components/AnimatorComponent.h"
#include "../runtime/AnimationRecordingState.h"
#include "../runtime/PlayState.h"
#include "../scene/ComponentFactory.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include "AssetDatabase.h"
#include "EditorApplication.h"
#include "EditorImGuiUtil.h"
#include "EditorSelection.h"
#include "SceneJsonImporter.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#endif // USE_IMGUI

namespace KujataEngine {

#ifdef USE_IMGUI

namespace {

constexpr float kTrackListWidth = 230.0f;
constexpr float kRulerHeight = 24.0f;
constexpr float kRowHeight = 22.0f;
constexpr float kKeyHalfSize = 5.0f;
constexpr float kTimeSnap = 0.01f;

// 選択GameObject(または祖先)のAnimatorComponentを返す(無ければnullptr)。
// Unity同様、子を選択していても親のAnimatorを編集対象にする。
// outRecordPrefixには、選択Objectが子孫の場合の相対パスprefix("子/孫:")が入る(録画時のチャンネル名用)。
AnimatorComponent* FindSelectedAnimator(GameObject*& outOwner, std::string* outRecordPrefix = nullptr) {
	outOwner = nullptr;
	if (outRecordPrefix) {
		outRecordPrefix->clear();
	}

	GameObject* selected = EditorSelection::GetInstance()->GetSelectedGameObject();
	if (!selected) {
		return nullptr;
	}

	for (GameObject* candidate = selected; candidate; candidate = candidate->GetParent()) {
		AnimatorComponent* animator = candidate->GetComponent<AnimatorComponent>();
		if (!animator) {
			continue;
		}

		outOwner = candidate;
		if (outRecordPrefix && candidate != selected) {
			// 選択Object→Animator所有Objectの相対パス("子/孫:")を作る。
			std::string relativePath;
			for (GameObject* node = selected; node && node != candidate; node = node->GetParent()) {
				if (relativePath.empty()) {
					relativePath = node->GetName();
				} else {
					relativePath = node->GetName() + "/" + relativePath;
				}
			}
			*outRecordPrefix = relativePath + ":";
		}
		return animator;
	}
	return nullptr;
}

float SnapTime(float time) {
	return std::round(time / kTimeSnap) * kTimeSnap;
}

// ドープシートの時間表示レンジ(秒)。クリップより少し広く、最低1秒。
float ComputeViewDuration(const AnimationClipData& clip) {
	return (std::max)(clip.GetDuration() * 1.15f, 1.0f);
}

// 向き基準移動/ローカル回転の仮想チャンネル(実フィールドを持たず、Animatorが直接解決・適用する)か。
bool IsLocalMoveChannelPath(const std::string& path) {
	if (path.ends_with("Transform/moveForward") || path.ends_with("Transform/moveRight")) {
		return true;
	}
	return path.ends_with("Transform/localRotation.x") || path.ends_with("Transform/localRotation.y") || path.ends_with("Transform/localRotation.z");
}

ImU32 KeyColor(bool selected) {
	return selected ? IM_COL32(255, 200, 60, 255) : IM_COL32(160, 200, 255, 255);
}

void DrawKeyDiamond(ImDrawList* drawList, const ImVec2& center, ImU32 color) {
	const float half = kKeyHalfSize;
	ImVec2 points[4] = {
	    {center.x, center.y - half},
	    {center.x + half, center.y},
	    {center.x, center.y + half},
	    {center.x - half, center.y},
	};
	drawList->AddConvexPolyFilled(points, 4, color);
	drawList->AddPolyline(points, 4, IM_COL32(30, 30, 30, 255), ImDrawFlags_Closed, 1.0f);
}

} // namespace

void AnimationWindow::CollectChannels(GameObject& owner, const AnimatorComponent& animator, std::vector<NamedChannel>& outChannels) const {
	// 自身+子孫の全チャンネルを列挙する(子孫は"子/孫:Component/チャンネル"形式)。Animatorの解決と同じロジック。
	std::vector<AnimatorChannel> channels;
	AnimatorComponent::CollectHierarchyChannels(owner, &animator, channels);
	for (const AnimatorChannel& channel : channels) {
		outChannels.push_back({channel.path, channel.value});
	}

	// 向き基準移動/ローカル回転の仮想チャンネルをAdd Track用に列挙する。
	// 実フィールドを持たない(value=nullptr)ため、値の編集は選択キーの数値エディタで行う。
	auto appendLocalMove = [&](auto&& self, GameObject& object, const std::string& objectPrefix) -> void {
		std::string transformPrefix = objectPrefix + "Transform/";
		outChannels.push_back({transformPrefix + "moveForward", nullptr});
		outChannels.push_back({transformPrefix + "moveRight", nullptr});
		outChannels.push_back({transformPrefix + "localRotation.x", nullptr});
		outChannels.push_back({transformPrefix + "localRotation.y", nullptr});
		outChannels.push_back({transformPrefix + "localRotation.z", nullptr});
		for (GameObject* child : object.GetChildren()) {
			if (!child) {
				continue;
			}
			std::string childPrefix;
			if (objectPrefix.empty()) {
				childPrefix = child->GetName() + ":";
			} else {
				childPrefix = objectPrefix.substr(0, objectPrefix.size() - 1) + "/" + child->GetName() + ":";
			}
			self(self, *child, childPrefix);
		}
	};
	appendLocalMove(appendLocalMove, owner, "");
}

float* AnimationWindow::FindChannelValue(const std::vector<NamedChannel>& channels, const std::string& path) const {
	for (const NamedChannel& channel : channels) {
		if (channel.path == path) {
			return channel.value;
		}
	}
	return nullptr;
}

void AnimationWindow::BeginPreview(Scene& scene, AnimatorComponent& animator) {
	if (previewing_) {
		return;
	}
	previewSnapshotJson_ = scene.ToJson();
	GameObject* owner = animator.GetOwner();
	previewOwnerInstanceId_ = owner ? owner->GetInstanceId() : std::string();
	previewing_ = true;
	previewPlaying_ = false;
	animator.ResolveBindings();
	// 加算トラックはプレビュー開始時点の値を基準にする。
	animator.ClearAdditiveBases();
	animator.EvaluateAt(previewTime_);
}

void AnimationWindow::EndPreview(Scene& scene) {
	if (!previewing_) {
		return;
	}
	previewing_ = false;
	previewPlaying_ = false;
	// 録画はプレビュー前提なので一緒に終了する。
	recording_ = false;
	SetAnimationRecording(false);

	// シーン復元でAnimatorも再構築される(クリップはファイルから再読込される)ため、
	// プレビュー中の未保存のクリップ編集を退避して復元後に引き継ぐ。
	AnimationClipData editedClip;
	std::string editedClipPath;
	bool hasEditedClip = false;
	{
		GameObject* owner = nullptr;
		AnimatorComponent* animator = FindSelectedAnimator(owner);
		if (animator && animator->HasClip() && owner && owner->GetInstanceId() == previewOwnerInstanceId_) {
			editedClip = animator->GetClip();
			editedClipPath = animator->GetClipPath();
			hasEditedClip = true;
		}
	}

	if (!previewSnapshotJson_.empty()) {
		// プレビューで書き換えた値をスナップショットへ巻き戻す。
		SceneJsonImporter::ApplySceneJsonString(scene, previewSnapshotJson_);
		scene.UpdateWorldTransforms();
		previewSnapshotJson_.clear();
	}

	if (hasEditedClip) {
		GameObject* restoredOwner = scene.FindGameObjectByInstanceId(previewOwnerInstanceId_);
		if (restoredOwner) {
			AnimatorComponent* restoredAnimator = restoredOwner->GetComponent<AnimatorComponent>();
			if (restoredAnimator && restoredAnimator->GetClipPath() == editedClipPath) {
				restoredAnimator->GetClip() = std::move(editedClip);
				restoredAnimator->ResolveBindings();
				restoredAnimator->ClearAdditiveBases();
			}
		}
	}

	previewOwnerInstanceId_.clear();
}

void AnimationWindow::UpdatePreview(AnimatorComponent& animator) {
	if (!previewing_) {
		return;
	}

	float duration = animator.GetClip().GetDuration();
	if (previewPlaying_ && duration > 0.0f) {
		float deltaTime = ImGui::GetIO().DeltaTime;
		switch (animator.GetClip().wrapMode) {
		case AnimationWrapMode::Once:
			previewTime_ += deltaTime;
			if (previewTime_ >= duration) {
				previewTime_ = duration;
				previewPlaying_ = false;
			}
			break;
		case AnimationWrapMode::Loop:
			previewTime_ = std::fmod(previewTime_ + deltaTime, duration);
			break;
		case AnimationWrapMode::PingPong:
			if (previewReverse_) {
				previewTime_ -= deltaTime;
				if (previewTime_ <= 0.0f) {
					previewTime_ = -previewTime_;
					previewReverse_ = false;
				}
			} else {
				previewTime_ += deltaTime;
				if (previewTime_ >= duration) {
					previewTime_ = duration - (previewTime_ - duration);
					previewReverse_ = true;
				}
			}
			previewTime_ = std::clamp(previewTime_, 0.0f, duration);
			break;
		}
	}

	animator.EvaluateAt(previewTime_);
}

bool AnimationWindow::IsKeySelected(int trackIndex, int keyIndex) const {
	for (const SelectedKeyRef& reference : selectedKeys_) {
		if (reference.trackIndex == trackIndex && reference.keyIndex == keyIndex) {
			return true;
		}
	}
	return false;
}

void AnimationWindow::CopySelectedKeys(const AnimationClipData& clip) {
	// 選択内の最早キーを基準(offset 0)として相対時刻で保存する。
	float baseTime = FLT_MAX;
	for (const SelectedKeyRef& reference : selectedKeys_) {
		if (reference.trackIndex < 0 || reference.trackIndex >= static_cast<int>(clip.tracks.size())) {
			continue;
		}
		const AnimationTrack& track = clip.tracks[reference.trackIndex];
		if (reference.keyIndex < 0 || reference.keyIndex >= static_cast<int>(track.curve.keys.size())) {
			continue;
		}
		baseTime = (std::min)(baseTime, track.curve.keys[reference.keyIndex].time);
	}
	if (baseTime == FLT_MAX) {
		return;
	}

	copiedKeys_.clear();
	for (const SelectedKeyRef& reference : selectedKeys_) {
		if (reference.trackIndex < 0 || reference.trackIndex >= static_cast<int>(clip.tracks.size())) {
			continue;
		}
		const AnimationTrack& track = clip.tracks[reference.trackIndex];
		if (reference.keyIndex < 0 || reference.keyIndex >= static_cast<int>(track.curve.keys.size())) {
			continue;
		}
		const AnimationKeyframe& key = track.curve.keys[reference.keyIndex];
		copiedKeys_.push_back({track.path, key.time - baseTime, key});
	}
	statusMessage_ = std::to_string(copiedKeys_.size()) + " key(s) copied.";
}

void AnimationWindow::PasteCopiedKeys(AnimationClipData& clip) {
	if (copiedKeys_.empty()) {
		return;
	}

	// 最早キーがプレイヘッドに来るよう、相対時刻を保ってコピー元トラックへ貼り付ける。
	for (const CopiedKeyEntry& entry : copiedKeys_) {
		AnimationTrack& track = clip.GetOrAddTrack(entry.trackPath);
		AnimationKeyframe key = entry.key;
		key.time = (std::max)(SnapTime(previewTime_ + entry.timeOffset), 0.0f);
		track.curve.AddKey(key);
	}
	selectedKeys_.clear();
	statusMessage_ = std::to_string(copiedKeys_.size()) + " key(s) pasted.";
}

void AnimationWindow::DeleteSelectedKeys(AnimationClipData& clip) {
	// index順が崩れないよう、トラックごとにkeyIndex降順で消す。
	std::vector<SelectedKeyRef> references = selectedKeys_;
	std::sort(references.begin(), references.end(), [](const SelectedKeyRef& a, const SelectedKeyRef& b) {
		if (a.trackIndex != b.trackIndex) {
			return a.trackIndex < b.trackIndex;
		}
		return a.keyIndex > b.keyIndex;
	});

	for (const SelectedKeyRef& reference : references) {
		if (reference.trackIndex < 0 || reference.trackIndex >= static_cast<int>(clip.tracks.size())) {
			continue;
		}
		AnimationTrack& track = clip.tracks[reference.trackIndex];
		if (reference.keyIndex < 0 || reference.keyIndex >= static_cast<int>(track.curve.keys.size())) {
			continue;
		}
		track.curve.keys.erase(track.curve.keys.begin() + reference.keyIndex);
	}
	selectedKeys_.clear();
	selectedKeyIndex_ = -1;
}

void AnimationWindow::GroupDragSelectedKeys(AnimationClipData& clip, float requestedDelta) {
	// 全選択キーが「未選択の隣接キー」を越えない範囲にdeltaをクランプする(選択同士は一緒に動くので制約しない)。
	float minDelta = -1.0e9f;
	float maxDelta = 1.0e9f;
	for (const SelectedKeyRef& reference : selectedKeys_) {
		if (reference.trackIndex < 0 || reference.trackIndex >= static_cast<int>(clip.tracks.size())) {
			continue;
		}
		AnimationTrack& track = clip.tracks[reference.trackIndex];
		if (reference.keyIndex < 0 || reference.keyIndex >= static_cast<int>(track.curve.keys.size())) {
			continue;
		}
		float keyTime = track.curve.keys[reference.keyIndex].time;

		int previousIndex = reference.keyIndex - 1;
		while (previousIndex >= 0 && IsKeySelected(reference.trackIndex, previousIndex)) {
			--previousIndex;
		}
		float lowerBound = (previousIndex >= 0) ? track.curve.keys[previousIndex].time + kTimeSnap : 0.0f;
		minDelta = (std::max)(minDelta, lowerBound - keyTime);

		int nextIndex = reference.keyIndex + 1;
		while (nextIndex < static_cast<int>(track.curve.keys.size()) && IsKeySelected(reference.trackIndex, nextIndex)) {
			++nextIndex;
		}
		if (nextIndex < static_cast<int>(track.curve.keys.size())) {
			maxDelta = (std::min)(maxDelta, track.curve.keys[nextIndex].time - kTimeSnap - keyTime);
		}
	}

	float delta = std::clamp(SnapTime(requestedDelta), minDelta, maxDelta);
	if (std::abs(delta) < 1.0e-6f) {
		return;
	}

	for (const SelectedKeyRef& reference : selectedKeys_) {
		if (reference.trackIndex < 0 || reference.trackIndex >= static_cast<int>(clip.tracks.size())) {
			continue;
		}
		AnimationTrack& track = clip.tracks[reference.trackIndex];
		if (reference.keyIndex < 0 || reference.keyIndex >= static_cast<int>(track.curve.keys.size())) {
			continue;
		}
		track.curve.keys[reference.keyIndex].time += delta;
	}
}

void AnimationWindow::AddKeyAtTime(AnimatorComponent& animator, const std::string& trackPath, const std::vector<NamedChannel>& channels, float time) {
	AnimationTrack& track = animator.GetClip().GetOrAddTrack(trackPath);

	AnimationKeyframe key;
	key.time = (std::max)(SnapTime(time), 0.0f);
	float* channelValue = FindChannelValue(channels, trackPath);
	if (channelValue) {
		key.value = *channelValue;
		if (track.additive) {
			// 加算トラックのキー値は絶対値ではなく「基準値からの差分」で記録する。
			// (絶対値で記録すると、Rec開始地点が原点以外の場合にその座標が二重加算される)
			float baseValue = 0.0f;
			if (animator.TryGetAdditiveBase(trackPath, baseValue)) {
				key.value = *channelValue - baseValue;
			} else {
				// 基準未キャプチャ(プレビュー外)では差分が定まらないため、カーブの現在値を維持する。
				key.value = track.curve.Evaluate(key.time);
			}
		}
	} else {
		key.value = track.curve.Evaluate(key.time);
	}

	int keyIndex = track.curve.AddKey(key);
	// 追加キーと前後キーのタンジェントを滑らかに整える。
	AnimationClipAsset::SetSmoothTangents(track.curve, keyIndex - 1);
	AnimationClipAsset::SetSmoothTangents(track.curve, keyIndex);
	AnimationClipAsset::SetSmoothTangents(track.curve, keyIndex + 1);
}

void AnimationWindow::DrawKeyPresetMenuItems(AnimationCurve& curve, int keyIndex) {
	if (keyIndex < 0 || keyIndex >= static_cast<int>(curve.keys.size())) {
		return;
	}
	AnimationKeyframe& key = curve.keys[keyIndex];

	// --- タンジェント系プリセット(イージング指定は解除してタンジェント補間に戻す) ---
	if (ImGui::MenuItem("Smooth")) {
		key.easing = EasingType::None;
		AnimationClipAsset::SetSmoothTangents(curve, keyIndex);
	}
	if (ImGui::MenuItem("Linear")) {
		key.easing = EasingType::None;
		AnimationClipAsset::SetLinearTangents(curve, keyIndex);
	}
	if (ImGui::MenuItem("Flat")) {
		key.easing = EasingType::None;
		key.inTangent = 0.0f;
		key.outTangent = 0.0f;
	}
	if (ImGui::MenuItem("Constant")) {
		key.easing = EasingType::None;
		key.inTangent = AnimationClipAsset::ConstantTangent();
		key.outTangent = AnimationClipAsset::ConstantTangent();
	}

	// --- easings.netのイージングテンプレ(このキー→次のキーの区間へ適用) ---
	ImGui::Separator();
	if (ImGui::BeginMenu("Easing (to Next Key)")) {
		struct EasingFamily {
			const char* name;
			EasingType easeIn;
			EasingType easeOut;
			EasingType easeInOut;
		};
		static constexpr EasingFamily kFamilies[] = {
		    {"Sine", EasingType::EaseInSine, EasingType::EaseOutSine, EasingType::EaseInOutSine},
		    {"Quad", EasingType::EaseInQuad, EasingType::EaseOutQuad, EasingType::EaseInOutQuad},
		    {"Cubic", EasingType::EaseInCubic, EasingType::EaseOutCubic, EasingType::EaseInOutCubic},
		    {"Quart", EasingType::EaseInQuart, EasingType::EaseOutQuart, EasingType::EaseInOutQuart},
		    {"Quint", EasingType::EaseInQuint, EasingType::EaseOutQuint, EasingType::EaseInOutQuint},
		    {"Expo", EasingType::EaseInExpo, EasingType::EaseOutExpo, EasingType::EaseInOutExpo},
		    {"Circ", EasingType::EaseInCirc, EasingType::EaseOutCirc, EasingType::EaseInOutCirc},
		    {"Back", EasingType::EaseInBack, EasingType::EaseOutBack, EasingType::EaseInOutBack},
		    {"Elastic", EasingType::EaseInElastic, EasingType::EaseOutElastic, EasingType::EaseInOutElastic},
		    {"Bounce", EasingType::EaseInBounce, EasingType::EaseOutBounce, EasingType::EaseInOutBounce},
		};

		if (ImGui::MenuItem("Tangent (Free)", nullptr, key.easing == EasingType::None)) {
			key.easing = EasingType::None;
		}
		ImGui::Separator();
		for (const EasingFamily& family : kFamilies) {
			if (ImGui::BeginMenu(family.name)) {
				if (ImGui::MenuItem("Ease In", nullptr, key.easing == family.easeIn)) {
					key.easing = family.easeIn;
				}
				if (ImGui::MenuItem("Ease Out", nullptr, key.easing == family.easeOut)) {
					key.easing = family.easeOut;
				}
				if (ImGui::MenuItem("Ease In Out", nullptr, key.easing == family.easeInOut)) {
					key.easing = family.easeInOut;
				}
				ImGui::EndMenu();
			}
		}
		ImGui::EndMenu();
	}
}

void AnimationWindow::DrawClipCreation(AnimatorComponent& animator) {
	ImGui::TextDisabled("No animation clip. Create one:");
	ImGui::InputText("Clip Name", newClipNameBuffer_.data(), newClipNameBuffer_.size());
	ImGui::SameLine();
	if (ImGui::Button("Create")) {
		std::string clipName = newClipNameBuffer_.data();
		if (clipName.empty()) {
			clipName = "NewAnimation";
		}

		std::filesystem::path projectRoot = GetProjectDataRoot();
		std::filesystem::path animationsDirectory = projectRoot / "Animations";
		std::error_code errorCode;
		std::filesystem::create_directories(animationsDirectory, errorCode);

		std::filesystem::path clipPath = animationsDirectory / (clipName + ".anim.json");
		std::string message;
		if (AnimationClipAsset::CreateDefaultFile(clipPath, message)) {
			AssetDatabase::GetInstance().GetOrCreateAssetId(clipPath);
			animator.SetClipPath(clipPath.generic_string());
			statusMessage_ = "Created: " + clipPath.filename().generic_string();
			ImGui::CloseCurrentPopup();
		} else {
			statusMessage_ = message;
		}
	}
}

void AnimationWindow::UpdateTimelineView(const ImVec2& canvasPosition, const ImVec2& canvasSize) {
	ImVec2 mouse = ImGui::GetMousePos();
	bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && mouse.x >= canvasPosition.x && mouse.x <= canvasPosition.x + canvasSize.x && mouse.y >= canvasPosition.y && mouse.y <= canvasPosition.y + canvasSize.y;
	if (!hovered) {
		return;
	}

	float wheel = ImGui::GetIO().MouseWheel;
	if (wheel != 0.0f) {
		if (ImGui::GetIO().KeyShift) {
			// Shift+ホイール: パン(1ノッチで80px分)。
			timelineScroll_ = (std::max)(0.0f, timelineScroll_ - wheel * 80.0f / pixelsPerSecond_);
		} else {
			// ホイール: マウス位置の時刻を保ったままズーム。
			float mouseTime = (mouse.x - canvasPosition.x - 6.0f) / pixelsPerSecond_ + timelineScroll_;
			pixelsPerSecond_ = std::clamp(pixelsPerSecond_ * (1.0f + wheel * 0.15f), 10.0f, 4000.0f);
			timelineScroll_ = (std::max)(0.0f, mouseTime - (mouse.x - canvasPosition.x - 6.0f) / pixelsPerSecond_);
		}
	}

	// 中ボタンドラッグ: パン。
	if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
		timelineScroll_ = (std::max)(0.0f, timelineScroll_ - ImGui::GetIO().MouseDelta.x / pixelsPerSecond_);
	}
}

void AnimationWindow::DrawTimelineRuler(ImDrawList* drawList, const ImVec2& canvasPosition, const ImVec2& canvasSize) {
	// ズームに応じて目盛り間隔を選ぶ(細目盛りが8px以上になる最小のステップ)。
	static constexpr float kStepCandidates[] = {0.01f, 0.05f, 0.1f, 0.5f, 1.0f, 5.0f, 10.0f, 30.0f};
	float minorStep = kStepCandidates[0];
	for (float candidate : kStepCandidates) {
		minorStep = candidate;
		if (candidate * pixelsPerSecond_ >= 8.0f) {
			break;
		}
	}

	float visibleDuration = (canvasSize.x - 12.0f) / pixelsPerSecond_;
	int firstIndex = (std::max)(0, static_cast<int>(std::floor(timelineScroll_ / minorStep)));
	int lastIndex = static_cast<int>(std::ceil((timelineScroll_ + visibleDuration) / minorStep)) + 1;

	for (int index = firstIndex; index <= lastIndex; ++index) {
		float tick = index * minorStep;
		float x = canvasPosition.x + 6.0f + (tick - timelineScroll_) * pixelsPerSecond_;
		if (x < canvasPosition.x || x > canvasPosition.x + canvasSize.x) {
			continue;
		}
		bool isMajor = (index % 5 == 0);
		drawList->AddLine(ImVec2(x, canvasPosition.y + (isMajor ? 4.0f : 12.0f)), ImVec2(x, canvasPosition.y + canvasSize.y), isMajor ? IM_COL32(110, 110, 115, 255) : IM_COL32(60, 60, 64, 255), 1.0f);
		if (isMajor) {
			char label[16];
			snprintf(label, sizeof(label), "%.4g", tick);
			drawList->AddText(ImVec2(x + 3.0f, canvasPosition.y + 2.0f), IM_COL32(170, 170, 175, 255), label);
		}
	}
}

void AnimationWindow::AcceptClipDrop(AnimatorComponent& animator) {
	if (!ImGui::BeginDragDropTarget()) {
		return;
	}
	const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAnimClipDragPayloadType);
	if (payload && payload->DataSize > 0) {
		animator.SetClipPath(static_cast<const char*>(payload->Data));
		selectedTrackIndex_ = -1;
		selectedKeyIndex_ = -1;
		selectedKeys_.clear();
		previewTime_ = 0.0f;
		previewReverse_ = false;
		statusMessage_ = "Clip added.";
	}
	ImGui::EndDragDropTarget();
}

void AnimationWindow::DrawClipBar(AnimatorComponent& animator) {
	const std::vector<AnimationClipReference>& references = animator.GetClipReferences();

	std::string currentLabel;
	if (animator.HasClip()) {
		currentLabel = animator.GetClip().name + "  (" + animator.GetClipPath() + ")";
	} else if (references.empty()) {
		currentLabel = "(no clip)";
	} else {
		currentLabel = animator.GetClipPath();
	}

	ImGui::SetNextItemWidth(340.0f);
	if (ImGui::BeginCombo("##ClipSelector", currentLabel.c_str())) {
		for (int index = 0; index < static_cast<int>(references.size()); ++index) {
			bool isSelected = (index == animator.GetCurrentClipIndex());
			std::string itemLabel = references[index].path + "##clip" + std::to_string(index);
			if (ImGui::Selectable(itemLabel.c_str(), isSelected) && !isSelected) {
				// 切替時、未保存のクリップ編集はファイルから再読込されて破棄される。
				animator.SetCurrentClipIndex(index);
				selectedTrackIndex_ = -1;
				selectedKeyIndex_ = -1;
				selectedKeys_.clear();
				previewTime_ = 0.0f;
				previewReverse_ = false;
				statusMessage_ = "Switched clip (unsaved edits discarded).";
			}
		}
		ImGui::EndCombo();
	}
	AcceptClipDrop(animator);

	ImGui::SameLine();
	if (ImGui::Button("+##NewClip")) {
		ImGui::OpenPopup("NewClipPopup");
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("新しいAnimation Clipを作成して追加");
	}
	if (ImGui::BeginPopup("NewClipPopup")) {
		DrawClipCreation(animator);
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	ImGui::TextDisabled("(Projectから.anim.jsonをD&Dで追加)");
}

void AnimationWindow::DrawToolbar(Scene& scene, AnimatorComponent& animator, const std::vector<NamedChannel>& channels) {
	// --- 表示切替(Unityのドープシート/カーブ切替に相当) ---
	if (ImGui::RadioButton("Dopesheet", viewMode_ == 0)) {
		viewMode_ = 0;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Curves", viewMode_ == 1)) {
		viewMode_ = 1;
	}
	ImGui::SameLine();
	if (ImGui::Button("Fit")) {
		// 次の描画でクリップ全体にフィットし直す。
		pixelsPerSecond_ = 0.0f;
		timelineScroll_ = 0.0f;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("タイムライン表示をクリップ全体へフィット\n(ホイール=ズーム / Shift+ホイール・中ボタンドラッグ=スクロール)");
	}
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();

	// --- 録画(Unityの赤丸)。録画ONはプレビューも自動でONにする ---
	bool wasRecording = recording_;
	if (wasRecording) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.18f, 0.18f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
	}
	if (ImGui::Button("[Rec]")) {
		if (recording_) {
			recording_ = false;
			SetAnimationRecording(false);
		} else {
			if (!previewing_) {
				BeginPreview(scene, animator);
			}
			recording_ = true;
			SetAnimationRecording(true);
		}
	}
	if (wasRecording) {
		ImGui::PopStyleColor(3);
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("録画モード: Inspectorで変更した値をプレイヘッドへ自動キー登録");
	}
	ImGui::SameLine();

	// --- プレビュー制御 ---
	bool previewToggled = previewing_;
	if (ImGui::Checkbox("Preview", &previewToggled)) {
		if (previewToggled) {
			BeginPreview(scene, animator);
		} else {
			EndPreview(scene);
		}
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(!previewing_);
	if (previewPlaying_) {
		if (ImGui::Button("Stop##PreviewPlay")) {
			previewPlaying_ = false;
		}
	} else {
		if (ImGui::Button("Play##PreviewPlay")) {
			previewPlaying_ = true;
			previewReverse_ = false;
			if (previewTime_ >= animator.GetClip().GetDuration()) {
				previewTime_ = 0.0f;
			}
		}
	}
	ImGui::EndDisabled();

	// --- 時刻 ---
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	float timeValue = previewTime_;
	if (ImGui::DragFloat("##Time", &timeValue, 0.01f, 0.0f, 0.0f, "%.2fs")) {
		previewTime_ = (std::max)(timeValue, 0.0f);
	}

	ImGui::SameLine();
	ImGui::Text("/ %.2fs", animator.GetClip().GetDuration());

	// --- ラップモード・キー追加・保存 ---
	ImGui::SameLine();
	const char* wrapItems[] = {"Once", "Loop", "PingPong"};
	int wrapIndex = static_cast<int>(animator.GetClip().wrapMode);
	ImGui::SetNextItemWidth(95.0f);
	if (ImGui::Combo("##WrapMode", &wrapIndex, wrapItems, 3)) {
		animator.GetClip().wrapMode = static_cast<AnimationWrapMode>(wrapIndex);
		previewReverse_ = false;
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(animator.GetClip().tracks.empty());
	if (ImGui::Button("Key All")) {
		for (const AnimationTrack& track : animator.GetClip().tracks) {
			AddKeyAtTime(animator, track.path, channels, previewTime_);
		}
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button("Save Clip")) {
		std::string message;
		if (animator.SaveClip(message)) {
			statusMessage_ = "Saved: " + animator.GetClipPath();
		} else {
			statusMessage_ = message;
		}
	}

	if (!statusMessage_.empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled("%s", statusMessage_.c_str());
	}
}

void AnimationWindow::DrawTrackList(AnimatorComponent& animator, const std::vector<NamedChannel>& channels) {
	AnimationClipData& clip = animator.GetClip();

	// トラック追加ポップアップ(まだトラックが無いチャンネルを列挙)。
	if (ImGui::Button("Add Track...", ImVec2(-1.0f, 0.0f))) {
		ImGui::OpenPopup("AddTrackPopup");
	}
	if (ImGui::BeginPopup("AddTrackPopup")) {
		bool anyCandidate = false;
		for (const NamedChannel& channel : channels) {
			if (clip.FindTrack(channel.path)) {
				continue;
			}
			anyCandidate = true;
			if (ImGui::MenuItem(channel.path.c_str())) {
				AddKeyAtTime(animator, channel.path, channels, previewTime_);
				selectedTrackIndex_ = static_cast<int>(clip.tracks.size()) - 1;
				ImGui::CloseCurrentPopup();
			}
		}
		if (!anyCandidate) {
			ImGui::TextDisabled("No more channels.");
		}
		ImGui::EndPopup();
	}

	// トラック一覧。
	int removeTrackIndex = -1;
	for (int trackIndex = 0; trackIndex < static_cast<int>(clip.tracks.size()); ++trackIndex) {
		const AnimationTrack& track = clip.tracks[trackIndex];
		ImGui::PushID(trackIndex);

		bool isSelected = (selectedTrackIndex_ == trackIndex);
		// 仮想チャンネル(向き基準移動)はfloat*を持たないが、Animatorが直接解決するためバインド済み扱い。
		bool isBound = FindChannelValue(channels, track.path) != nullptr || IsLocalMoveChannelPath(track.path);
		if (!isBound) {
			// 解決できないトラック(Component欠落など)は赤字で警告表示。
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
		}
		// 加算モードのトラックは[+]マーカー付きで表示する。
		std::string trackLabel = track.additive ? ("[+] " + track.path) : track.path;
		if (ImGui::Selectable(trackLabel.c_str(), isSelected, 0, ImVec2(0.0f, kRowHeight - 4.0f))) {
			if (selectedTrackIndex_ != trackIndex) {
				selectedKeyIndex_ = -1;
			}
			selectedTrackIndex_ = trackIndex;
		}
		if (!isBound) {
			ImGui::PopStyleColor();
		}

		if (ImGui::BeginPopupContextItem("TrackContext")) {
			if (ImGui::MenuItem("Additive", nullptr, track.additive)) {
				// 加算モード切替。基準値を取り直す(プレビュー中は現在値が新基準になる点に注意)。
				AnimationTrack& mutableTrack = clip.tracks[trackIndex];
				mutableTrack.additive = !mutableTrack.additive;
				if (mutableTrack.additive && !mutableTrack.curve.keys.empty()) {
					// 絶対値で作られた既存キーを「開始時点(t=0)からの差分」へ正規化する。
					// これをしないと絶対座標がそのまま加算されて二重オフセットになる。
					float originValue = mutableTrack.curve.Evaluate(0.0f);
					for (AnimationKeyframe& normalizeKey : mutableTrack.curve.keys) {
						normalizeKey.value -= originValue;
					}
					statusMessage_ = "Additive: keys normalized to delta (t=0 origin).";
				}
				animator.ClearAdditiveBases();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Add Key At Playhead")) {
				// channelsはconst参照でAddKeyAtTimeが解決する。
				AddKeyAtTime(animator, track.path, channels, previewTime_);
			}
			bool canPaste = !copiedKeys_.empty();
			if (copiedKeys_.size() <= 1) {
				// 単一コピーは「このトラックへ」貼り付ける(別トラックへの貼り付けが可能)。
				if (ImGui::MenuItem("Paste Key At Playhead", nullptr, false, canPaste)) {
					AnimationKeyframe pastedKey = copiedKeys_.front().key;
					pastedKey.time = (std::max)(SnapTime(previewTime_), 0.0f);
					clip.tracks[trackIndex].curve.AddKey(pastedKey);
					selectedKeys_.clear();
				}
			} else {
				// 複数コピーは相対時刻を保ってコピー元トラックへ貼り付ける。
				if (ImGui::MenuItem("Paste Keys At Playhead", nullptr, false, canPaste)) {
					PasteCopiedKeys(clip);
				}
			}
			if (ImGui::MenuItem("Delete Track")) {
				removeTrackIndex = trackIndex;
			}
			ImGui::EndPopup();
		}

		ImGui::PopID();
	}

	if (removeTrackIndex >= 0) {
		clip.tracks.erase(clip.tracks.begin() + removeTrackIndex);
		if (selectedTrackIndex_ >= static_cast<int>(clip.tracks.size())) {
			selectedTrackIndex_ = static_cast<int>(clip.tracks.size()) - 1;
		}
		// trackIndex参照が崩れるため複数選択は解除する。
		selectedKeys_.clear();
	}

	// --- 選択キーの数値編集 ---
	// タイムライン上での値ドラッグは不可(イージング編集専用)のため、値はここで明示的に編集する。
	// 実フィールドを持たない仮想チャンネル(moveForward等)の値入力もここが入口になる。
	if (selectedTrackIndex_ >= 0 && selectedTrackIndex_ < static_cast<int>(clip.tracks.size())) {
		AnimationTrack& selectedTrack = clip.tracks[selectedTrackIndex_];
		if (selectedKeyIndex_ >= 0 && selectedKeyIndex_ < static_cast<int>(selectedTrack.curve.keys.size())) {
			AnimationKeyframe& key = selectedTrack.curve.keys[selectedKeyIndex_];
			ImGui::Separator();
			ImGui::TextDisabled("Selected Key");

			float keyTime = key.time;
			ImGui::SetNextItemWidth(90.0f);
			if (ImGui::DragFloat("Time##SelectedKey", &keyTime, 0.01f, 0.0f, 0.0f, "%.2fs")) {
				// 並び順を保つため前後キーの間へクランプする。
				float minTime = (selectedKeyIndex_ > 0) ? selectedTrack.curve.keys[selectedKeyIndex_ - 1].time + kTimeSnap : 0.0f;
				float maxTime = (selectedKeyIndex_ + 1 < static_cast<int>(selectedTrack.curve.keys.size())) ? selectedTrack.curve.keys[selectedKeyIndex_ + 1].time - kTimeSnap : 1.0e6f;
				key.time = std::clamp(SnapTime(keyTime), minTime, maxTime);
			}

			ImGui::SetNextItemWidth(90.0f);
			ImGui::DragFloat("Value##SelectedKey", &key.value, 0.01f);
		}
	}
}

void AnimationWindow::DrawDopeSheet(AnimatorComponent& animator, const std::vector<NamedChannel>& channels) {
	AnimationClipData& clip = animator.GetClip();
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	const ImVec2 canvasPosition = ImGui::GetCursorScreenPos();
	ImVec2 canvasSize = ImGui::GetContentRegionAvail();
	canvasSize.x = (std::max)(canvasSize.x, 50.0f);
	canvasSize.y = (std::max)(canvasSize.y, kRulerHeight + kRowHeight);

	// 初回(または明示リセット後)はクリップ長にフィットさせる。以降はズーム/スクロール値を使う。
	if (pixelsPerSecond_ <= 0.0f) {
		pixelsPerSecond_ = (canvasSize.x - 12.0f) / ComputeViewDuration(clip);
		timelineScroll_ = 0.0f;
	}
	UpdateTimelineView(canvasPosition, canvasSize);

	const float pixelsPerSecond = pixelsPerSecond_;
	auto timeToX = [&](float time) { return canvasPosition.x + 6.0f + (time - timelineScroll_) * pixelsPerSecond; };
	auto xToTime = [&](float x) { return (x - canvasPosition.x - 6.0f) / pixelsPerSecond + timelineScroll_; };

	// 背景。
	drawList->AddRectFilled(canvasPosition, ImVec2(canvasPosition.x + canvasSize.x, canvasPosition.y + canvasSize.y), IM_COL32(28, 28, 30, 255));

	// --- 目盛り(可視範囲のみ・ズーム対応) ---
	DrawTimelineRuler(drawList, canvasPosition, canvasSize);

	// --- ルーラーでのスクラブ(ドラッグで時刻変更) ---
	ImGui::SetCursorScreenPos(canvasPosition);
	ImGui::InvisibleButton("##DopeSheetRuler", ImVec2(canvasSize.x, kRulerHeight));
	if (ImGui::IsItemActive()) {
		previewTime_ = (std::max)(SnapTime(xToTime(ImGui::GetMousePos().x)), 0.0f);
		previewPlaying_ = false;
	}

	// --- 各トラック行とキー ---
	bool keyInteracted = false; // このフレームでキー上のクリック/ドラッグがあったか(範囲選択開始の抑制用)
	int trackCount = static_cast<int>(clip.tracks.size());
	for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
		AnimationTrack& track = clip.tracks[trackIndex];
		float rowTop = canvasPosition.y + kRulerHeight + trackIndex * kRowHeight;
		float rowCenterY = rowTop + kRowHeight * 0.5f;

		// 行の背景(選択中は明るく)。
		ImU32 rowColor = (trackIndex == selectedTrackIndex_) ? IM_COL32(52, 60, 74, 255) : ((trackIndex % 2 == 0) ? IM_COL32(36, 36, 40, 255) : IM_COL32(32, 32, 36, 255));
		drawList->AddRectFilled(ImVec2(canvasPosition.x, rowTop), ImVec2(canvasPosition.x + canvasSize.x, rowTop + kRowHeight), rowColor);

		// 行のダブルクリックでその時刻へキー追加。
		// 後からsubmitするキーのボタンがクリックを受け取れるよう、行ボタンはオーバーラップを許可する
		// (これが無いと先にsubmitされた行がクリックを奪い、キーをドラッグできない)。
		ImGui::SetCursorScreenPos(ImVec2(canvasPosition.x, rowTop));
		ImGui::PushID(trackIndex);
		ImGui::SetNextItemAllowOverlap();
		ImGui::InvisibleButton("##Row", ImVec2(canvasSize.x, kRowHeight));
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			selectedTrackIndex_ = trackIndex;
			AddKeyAtTime(animator, track.path, channels, xToTime(ImGui::GetMousePos().x));
		}
		if (ImGui::IsItemClicked()) {
			if (selectedTrackIndex_ != trackIndex) {
				selectedKeyIndex_ = -1;
			}
			selectedTrackIndex_ = trackIndex;
			// 空白クリックで複数選択を解除(Ctrl押下中は維持=範囲選択の追加用)。
			if (!ImGui::GetIO().KeyCtrl) {
				selectedKeys_.clear();
			}
		}

		// キー(ひし形)。ドラッグで時刻変更(前後キーの間にクランプ)、右クリックで削除。
		int removeKeyIndex = -1;
		for (int keyIndex = 0; keyIndex < static_cast<int>(track.curve.keys.size()); ++keyIndex) {
			AnimationKeyframe& key = track.curve.keys[keyIndex];
			ImVec2 keyCenter(timeToX(key.time), rowCenterY);

			// スクロール/ズームで画面外のキーは描画も判定もしない。
			if (keyCenter.x < canvasPosition.x - 8.0f || keyCenter.x > canvasPosition.x + canvasSize.x + 8.0f) {
				continue;
			}

			ImGui::PushID(keyIndex);
			ImGui::SetCursorScreenPos(ImVec2(keyCenter.x - kKeyHalfSize - 2.0f, keyCenter.y - kKeyHalfSize - 2.0f));
			ImGui::InvisibleButton("##Key", ImVec2((kKeyHalfSize + 2.0f) * 2.0f, (kKeyHalfSize + 2.0f) * 2.0f));

			bool keyActive = ImGui::IsItemActive();
			if (keyActive || ImGui::IsItemHovered()) {
				keyInteracted = true;
			}

			// クリックで単独選択、Ctrl+クリックで選択に追加/解除。
			if (ImGui::IsItemClicked()) {
				if (ImGui::GetIO().KeyCtrl) {
					if (IsKeySelected(trackIndex, keyIndex)) {
						std::erase_if(selectedKeys_, [&](const SelectedKeyRef& r) { return r.trackIndex == trackIndex && r.keyIndex == keyIndex; });
					} else {
						selectedKeys_.push_back({trackIndex, keyIndex});
					}
				} else if (!IsKeySelected(trackIndex, keyIndex)) {
					selectedKeys_.clear();
					selectedKeys_.push_back({trackIndex, keyIndex});
				}
				selectedTrackIndex_ = trackIndex;
				selectedKeyIndex_ = keyIndex;
			}

			if (keyActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
				float newTime = SnapTime(xToTime(ImGui::GetMousePos().x));
				if (selectedKeys_.size() > 1 && IsKeySelected(trackIndex, keyIndex)) {
					// 複数選択中はまとめて移動(未選択の隣接キーを越えない範囲で)。
					GroupDragSelectedKeys(clip, newTime - key.time);
				} else {
					// 並び順を保つため前後キーの間へクランプする。
					float minTime = (keyIndex > 0) ? track.curve.keys[keyIndex - 1].time + kTimeSnap : 0.0f;
					float maxTime = (keyIndex + 1 < static_cast<int>(track.curve.keys.size())) ? track.curve.keys[keyIndex + 1].time - kTimeSnap : 1.0e6f;
					key.time = std::clamp(newTime, minTime, maxTime);
				}
			}

			if (ImGui::BeginPopupContextItem("KeyContext")) {
				bool multiSelected = selectedKeys_.size() > 1 && IsKeySelected(trackIndex, keyIndex);
				if (ImGui::MenuItem(multiSelected ? "Copy Selected Keys" : "Copy Key")) {
					if (!multiSelected) {
						selectedKeys_.clear();
						selectedKeys_.push_back({trackIndex, keyIndex});
					}
					CopySelectedKeys(clip);
				}
				ImGui::Separator();
				DrawKeyPresetMenuItems(track.curve, keyIndex);
				ImGui::Separator();
				if (multiSelected) {
					if (ImGui::MenuItem("Delete Selected Keys")) {
						DeleteSelectedKeys(clip);
					}
				} else if (ImGui::MenuItem("Delete Key")) {
					removeKeyIndex = keyIndex;
				}
				ImGui::EndPopup();
			}

			DrawKeyDiamond(drawList, keyCenter, KeyColor(keyActive || IsKeySelected(trackIndex, keyIndex) || trackIndex == selectedTrackIndex_));
			ImGui::PopID();
		}

		if (removeKeyIndex >= 0) {
			track.curve.keys.erase(track.curve.keys.begin() + removeKeyIndex);
			// index参照が崩れるため複数選択は解除する。
			selectedKeys_.clear();
		}

		ImGui::PopID();
	}

	// --- 範囲選択(ラバーバンド)。空白から左ドラッグで開始し、矩形内のキーを選択する ---
	{
		ImVec2 mouse = ImGui::GetMousePos();
		bool inRowsArea = mouse.x >= canvasPosition.x && mouse.x <= canvasPosition.x + canvasSize.x && mouse.y >= canvasPosition.y + kRulerHeight && mouse.y <= canvasPosition.y + canvasSize.y;

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && inRowsArea && !keyInteracted && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
			boxSelectPending_ = true;
			boxSelectStartX_ = mouse.x;
			boxSelectStartY_ = mouse.y;
		}
		if (boxSelectPending_ && !boxSelecting_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
			boxSelecting_ = true;
		}

		if (boxSelecting_) {
			ImVec2 rectMin((std::min)(boxSelectStartX_, mouse.x), (std::min)(boxSelectStartY_, mouse.y));
			ImVec2 rectMax((std::max)(boxSelectStartX_, mouse.x), (std::max)(boxSelectStartY_, mouse.y));
			drawList->AddRectFilled(rectMin, rectMax, IM_COL32(120, 170, 255, 40));
			drawList->AddRect(rectMin, rectMax, IM_COL32(120, 170, 255, 180));

			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
				if (!ImGui::GetIO().KeyCtrl) {
					selectedKeys_.clear();
				}
				for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
					float rowCenterY = canvasPosition.y + kRulerHeight + trackIndex * kRowHeight + kRowHeight * 0.5f;
					if (rowCenterY < rectMin.y || rowCenterY > rectMax.y) {
						continue;
					}
					const AnimationTrack& selectTrack = clip.tracks[trackIndex];
					for (int keyIndex = 0; keyIndex < static_cast<int>(selectTrack.curve.keys.size()); ++keyIndex) {
						float keyX = timeToX(selectTrack.curve.keys[keyIndex].time);
						if (keyX < rectMin.x || keyX > rectMax.x) {
							continue;
						}
						if (!IsKeySelected(trackIndex, keyIndex)) {
							selectedKeys_.push_back({trackIndex, keyIndex});
						}
					}
				}
				boxSelecting_ = false;
				boxSelectPending_ = false;
			}
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			boxSelectPending_ = false;
		}
	}

	// --- プレイヘッド(最前面) ---
	float playheadX = timeToX(previewTime_);
	drawList->AddLine(ImVec2(playheadX, canvasPosition.y), ImVec2(playheadX, canvasPosition.y + canvasSize.y), IM_COL32(255, 90, 90, 255), 2.0f);
	drawList->AddTriangleFilled(ImVec2(playheadX - 5.0f, canvasPosition.y), ImVec2(playheadX + 5.0f, canvasPosition.y), ImVec2(playheadX, canvasPosition.y + 8.0f), IM_COL32(255, 90, 90, 255));

	// キャンバス領域をアイテムとして確保する。
	// SetCursorScreenPosだけで境界を広げるとimguiのアサート(SetCursorPos to extend boundaries)になるため、
	// 必ずDummyを最後に置いてウィンドウ境界を正しく成長させる。
	ImGui::SetCursorScreenPos(canvasPosition);
	ImGui::Dummy(canvasSize);
	AcceptClipDrop(animator);
}

void AnimationWindow::DrawCurveEditor(AnimatorComponent& animator, const std::vector<NamedChannel>& channels) {
	(void)channels;
	AnimationClipData& clip = animator.GetClip();
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	const ImVec2 canvasPosition = ImGui::GetCursorScreenPos();
	ImVec2 canvasSize = ImGui::GetContentRegionAvail();
	canvasSize.x = (std::max)(canvasSize.x, 50.0f);
	canvasSize.y = (std::max)(canvasSize.y, kRulerHeight + 80.0f);

	drawList->AddRectFilled(canvasPosition, ImVec2(canvasPosition.x + canvasSize.x, canvasPosition.y + canvasSize.y), IM_COL32(24, 24, 26, 255));

	if (selectedTrackIndex_ < 0 || selectedTrackIndex_ >= static_cast<int>(clip.tracks.size())) {
		drawList->AddText(ImVec2(canvasPosition.x + 12.0f, canvasPosition.y + kRulerHeight + 8.0f), IM_COL32(150, 150, 155, 255), "Select a track to edit its curve.");
		ImGui::SetCursorScreenPos(canvasPosition);
		ImGui::Dummy(canvasSize);
		return;
	}

	AnimationTrack& track = clip.tracks[selectedTrackIndex_];
	AnimationCurve& curve = track.curve;

	// 初回(または明示リセット後)はクリップ長にフィット。以降はズーム/スクロール値を使う。
	if (pixelsPerSecond_ <= 0.0f) {
		pixelsPerSecond_ = (canvasSize.x - 12.0f) / ComputeViewDuration(clip);
		timelineScroll_ = 0.0f;
	}
	UpdateTimelineView(canvasPosition, canvasSize);

	const float pixelsPerSecond = pixelsPerSecond_;
	const float visibleDuration = (canvasSize.x - 12.0f) / pixelsPerSecond;
	auto timeToX = [&](float time) { return canvasPosition.x + 6.0f + (time - timelineScroll_) * pixelsPerSecond; };
	auto xToTime = [&](float x) { return (x - canvasPosition.x - 6.0f) / pixelsPerSecond + timelineScroll_; };

	// --- 値レンジのオートフィット(キー値 + 可視範囲のカーブサンプル値から算出) ---
	float minValue = FLT_MAX;
	float maxValue = -FLT_MAX;
	for (const AnimationKeyframe& key : curve.keys) {
		minValue = (std::min)(minValue, key.value);
		maxValue = (std::max)(maxValue, key.value);
	}
	constexpr int kFitSampleCount = 64;
	for (int i = 0; i <= kFitSampleCount; ++i) {
		float sampled = curve.Evaluate(timelineScroll_ + visibleDuration * static_cast<float>(i) / kFitSampleCount);
		minValue = (std::min)(minValue, sampled);
		maxValue = (std::max)(maxValue, sampled);
	}
	if (curve.keys.empty()) {
		minValue = 0.0f;
		maxValue = 1.0f;
	}
	float valueRange = maxValue - minValue;
	if (valueRange < 1.0e-3f) {
		minValue -= 0.5f;
		maxValue += 0.5f;
	} else {
		minValue -= valueRange * 0.15f;
		maxValue += valueRange * 0.15f;
	}
	valueRange = maxValue - minValue;

	const float plotTop = canvasPosition.y + kRulerHeight;
	const float plotHeight = canvasSize.y - kRulerHeight - 4.0f;
	const float pixelsPerValue = plotHeight / valueRange;
	auto valueToY = [&](float value) { return plotTop + (maxValue - value) * pixelsPerValue; };

	// --- 時間目盛り(ドープシートと同じ・ズーム対応) ---
	DrawTimelineRuler(drawList, canvasPosition, canvasSize);

	// --- 値グリッド(1/2/5系列の切りの良い間隔) ---
	{
		float rawStep = valueRange / 5.0f;
		float magnitude = std::pow(10.0f, std::floor(std::log10((std::max)(rawStep, 1.0e-6f))));
		float normalized = rawStep / magnitude;
		float niceStep = (normalized < 1.5f ? 1.0f : normalized < 3.5f ? 2.0f : normalized < 7.5f ? 5.0f : 10.0f) * magnitude;
		for (float gridValue = std::ceil(minValue / niceStep) * niceStep; gridValue <= maxValue + 1.0e-6f; gridValue += niceStep) {
			float y = valueToY(gridValue);
			bool isZero = std::abs(gridValue) < niceStep * 0.5f;
			drawList->AddLine(ImVec2(canvasPosition.x, y), ImVec2(canvasPosition.x + canvasSize.x, y), isZero ? IM_COL32(110, 110, 90, 255) : IM_COL32(52, 52, 56, 255), 1.0f);
			char label[24];
			snprintf(label, sizeof(label), "%.3g", gridValue);
			drawList->AddText(ImVec2(canvasPosition.x + 4.0f, y - 14.0f), IM_COL32(140, 140, 145, 255), label);
		}
	}

	// --- ルーラーでのスクラブ ---
	ImGui::SetCursorScreenPos(canvasPosition);
	ImGui::InvisibleButton("##CurveRuler", ImVec2(canvasSize.x, kRulerHeight));
	if (ImGui::IsItemActive()) {
		previewTime_ = (std::max)(SnapTime(xToTime(ImGui::GetMousePos().x)), 0.0f);
		previewPlaying_ = false;
	}

	// --- プロット領域(ダブルクリックでキー追加 / クリックでキー選択解除) ---
	// カーブビューはイージング編集専用。値はカーブ上の現在値を使い、直接指定はさせない。
	ImGui::SetCursorScreenPos(ImVec2(canvasPosition.x, plotTop));
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton("##CurvePlot", ImVec2(canvasSize.x, plotHeight));
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		AnimationKeyframe key;
		key.time = (std::max)(SnapTime(xToTime(ImGui::GetMousePos().x)), 0.0f);
		key.value = curve.Evaluate(key.time);
		int keyIndex = curve.AddKey(key);
		AnimationClipAsset::SetSmoothTangents(curve, keyIndex - 1);
		AnimationClipAsset::SetSmoothTangents(curve, keyIndex);
		AnimationClipAsset::SetSmoothTangents(curve, keyIndex + 1);
		selectedKeyIndex_ = keyIndex;
	} else if (ImGui::IsItemClicked()) {
		selectedKeyIndex_ = -1;
	}

	// --- カーブ本体(2pxごとにサンプルして折れ線描画) ---
	if (!curve.keys.empty()) {
		const float plotLeft = canvasPosition.x + 2.0f;
		const float plotRight = canvasPosition.x + canvasSize.x - 2.0f;
		for (float x = plotLeft; x < plotRight; x += 2.0f) {
			drawList->PathLineTo(ImVec2(x, valueToY(curve.Evaluate(xToTime(x)))));
		}
		drawList->PathStroke(IM_COL32(120, 220, 140, 255), ImDrawFlags_None, 2.0f);
	}

	// --- キー(2Dドラッグ=時刻+値、右クリックでプリセット/削除) ---
	int removeKeyIndex = -1;
	for (int keyIndex = 0; keyIndex < static_cast<int>(curve.keys.size()); ++keyIndex) {
		AnimationKeyframe& key = curve.keys[keyIndex];
		ImVec2 keyCenter(timeToX(key.time), valueToY(key.value));

		// スクロール/ズームで画面外のキーは描画も判定もしない。
		if (keyCenter.x < canvasPosition.x - 8.0f || keyCenter.x > canvasPosition.x + canvasSize.x + 8.0f) {
			continue;
		}

		ImGui::PushID(keyIndex);
		ImGui::SetCursorScreenPos(ImVec2(keyCenter.x - kKeyHalfSize - 2.0f, keyCenter.y - kKeyHalfSize - 2.0f));
		ImGui::InvisibleButton("##CurveKey", ImVec2((kKeyHalfSize + 2.0f) * 2.0f, (kKeyHalfSize + 2.0f) * 2.0f));

		bool keyActive = ImGui::IsItemActive();
		if (ImGui::IsItemClicked() || keyActive) {
			selectedKeyIndex_ = keyIndex;
		}
		// カーブビューでは値は変更させない(イージング編集専用)。ドラッグは時刻のみ。
		if (keyActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
			float newTime = SnapTime(xToTime(ImGui::GetMousePos().x));
			float minTime = (keyIndex > 0) ? curve.keys[keyIndex - 1].time + kTimeSnap : 0.0f;
			float maxTime = (keyIndex + 1 < static_cast<int>(curve.keys.size())) ? curve.keys[keyIndex + 1].time - kTimeSnap : 1.0e6f;
			key.time = std::clamp(newTime, minTime, maxTime);
		}

		if (ImGui::BeginPopupContextItem("CurveKeyContext")) {
			selectedKeyIndex_ = keyIndex;
			if (ImGui::MenuItem("Copy Key")) {
				copiedKeys_.clear();
				copiedKeys_.push_back({track.path, 0.0f, key});
			}
			ImGui::Separator();
			DrawKeyPresetMenuItems(curve, keyIndex);
			ImGui::Separator();
			if (ImGui::MenuItem("Delete Key")) {
				removeKeyIndex = keyIndex;
			}
			ImGui::EndPopup();
		}

		DrawKeyDiamond(drawList, keyCenter, KeyColor(keyIndex == selectedKeyIndex_ || keyActive));
		ImGui::PopID();
	}

	// --- 選択キーのタンジェントハンドル(画面内のみ) ---
	if (selectedKeyIndex_ >= 0 && selectedKeyIndex_ < static_cast<int>(curve.keys.size()) && timeToX(curve.keys[selectedKeyIndex_].time) >= canvasPosition.x - 8.0f &&
	    timeToX(curve.keys[selectedKeyIndex_].time) <= canvasPosition.x + canvasSize.x + 8.0f) {
		AnimationKeyframe& key = curve.keys[selectedKeyIndex_];
		ImVec2 keyCenter(timeToX(key.time), valueToY(key.value));
		constexpr float kHandleLength = 55.0f;
		constexpr float kGripHalfSize = 6.0f;
		const ImU32 handleColor = IM_COL32(230, 230, 235, 255);

		// tangent(値/秒)をスクリーン方向ベクトルへ変換して固定長のハンドルにする。
		auto drawHandle = [&](float tangent, bool isOut, const char* id) {
			if (!std::isfinite(tangent)) {
				return; // Constant(ステップ)はハンドル無し。プリセットで解除する。
			}
			float directionSign = isOut ? 1.0f : -1.0f;
			ImVec2 direction(pixelsPerSecond * directionSign, -tangent * pixelsPerValue * directionSign);
			float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
			if (length < 1.0e-3f) {
				return;
			}
			ImVec2 grip(keyCenter.x + direction.x / length * kHandleLength, keyCenter.y + direction.y / length * kHandleLength);

			drawList->AddLine(keyCenter, grip, handleColor, 1.0f);
			drawList->AddCircleFilled(grip, 4.0f, handleColor);

			ImGui::SetCursorScreenPos(ImVec2(grip.x - kGripHalfSize, grip.y - kGripHalfSize));
			ImGui::InvisibleButton(id, ImVec2(kGripHalfSize * 2.0f, kGripHalfSize * 2.0f));
			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
				ImVec2 mouse = ImGui::GetMousePos();
				float deltaXPixels = mouse.x - keyCenter.x;
				// ハンドルの向き(in=左/out=右)を維持しつつ、ほぼ垂直ドラッグでも発散しないよう最小幅を確保。
				deltaXPixels = isOut ? (std::max)(deltaXPixels, 4.0f) : (std::min)(deltaXPixels, -4.0f);
				float deltaYPixels = mouse.y - keyCenter.y;
				float newTangent = (-deltaYPixels / pixelsPerValue) / (deltaXPixels / pixelsPerSecond);
				if (isOut) {
					key.outTangent = newTangent;
				} else {
					key.inTangent = newTangent;
				}
			}
		};

		// イージング指定された区間はタンジェント補間ではないためハンドルを出さない。
		// in側は前のキーのeasing、out側は自分のeasingが対象区間を決める。
		bool inSegmentIsTangent = (selectedKeyIndex_ == 0) || (curve.keys[selectedKeyIndex_ - 1].easing == EasingType::None);
		bool outSegmentIsTangent = (key.easing == EasingType::None);
		if (inSegmentIsTangent) {
			drawHandle(key.inTangent, false, "##InHandle");
		}
		if (outSegmentIsTangent) {
			drawHandle(key.outTangent, true, "##OutHandle");
		}
	}

	if (removeKeyIndex >= 0) {
		curve.keys.erase(curve.keys.begin() + removeKeyIndex);
		if (selectedKeyIndex_ >= static_cast<int>(curve.keys.size())) {
			selectedKeyIndex_ = static_cast<int>(curve.keys.size()) - 1;
		}
	}

	// --- プレイヘッド ---
	float playheadX = timeToX(previewTime_);
	drawList->AddLine(ImVec2(playheadX, canvasPosition.y), ImVec2(playheadX, canvasPosition.y + canvasSize.y), IM_COL32(255, 90, 90, 255), 2.0f);
	drawList->AddTriangleFilled(ImVec2(playheadX - 5.0f, canvasPosition.y), ImVec2(playheadX + 5.0f, canvasPosition.y), ImVec2(playheadX, canvasPosition.y + 8.0f), IM_COL32(255, 90, 90, 255));

	ImGui::SetCursorScreenPos(canvasPosition);
	ImGui::Dummy(canvasSize);
	AcceptClipDrop(animator);
}

void AnimationWindow::Draw(bool* pOpen) {
	// クリップを開けた場合のみ後段で有効化する(早期returnでは常に無効)。
	SetAnimationKeyContextEnabled(false);

	ImGui::Begin("Animation", pOpen);

	Scene* scene = EditorApplication::GetInstance()->GetCurrentScene();
	if (!scene) {
		ImGui::TextDisabled("No Scene.");
		ImGui::End();
		return;
	}

	// Play中はプレビューと編集を止める(ランタイム再生と競合するため)。
	if (IsGamePlaying()) {
		if (previewing_) {
			// Play開始時のシーン再構築で状態は上書きされるため、スナップショットは破棄する。
			previewing_ = false;
			previewPlaying_ = false;
			previewSnapshotJson_.clear();
			previewOwnerInstanceId_.clear();
		}
		recording_ = false;
		SetAnimationRecording(false);
		ImGui::TextDisabled("Playing... (editing disabled)");
		ImGui::End();
		return;
	}

	GameObject* owner = nullptr;
	std::string recordPathPrefix;
	AnimatorComponent* animator = FindSelectedAnimator(owner, &recordPathPrefix);

	// プレビュー対象が選択解除/変更されたら巻き戻す。
	if (previewing_) {
		bool ownerChanged = !owner || owner->GetInstanceId() != previewOwnerInstanceId_;
		if (ownerChanged || !animator) {
			EndPreview(*scene);
			// EndPreviewでシーンが再構築されるためポインタを取り直す。
			animator = FindSelectedAnimator(owner, &recordPathPrefix);
		}
	}

	if (!owner) {
		ImGui::TextDisabled("Select a GameObject.");
		ImGui::End();
		return;
	}

	if (!animator) {
		ImGui::TextDisabled("Selected object has no Animator.");
		if (ImGui::Button("Add AnimatorComponent")) {
			Component* added = owner->AddComponent(ComponentFactory::GetInstance().Create("AnimatorComponent"));
			scene->OnEditorComponentAdded(owner, added);
		}
		ImGui::End();
		return;
	}

	DrawClipBar(*animator);

	if (!animator->HasClip()) {
		ImGui::TextDisabled("No clip selected. [+]で作成するか、Projectから.anim.jsonをドロップしてください。");
		ImGui::End();
		return;
	}

	std::vector<NamedChannel> channels;
	CollectChannels(*owner, *animator, channels);

	// この時点でクリップ編集可能なので、Inspector側のAdd Keyframe/録画を有効化する。
	SetAnimationKeyContextEnabled(true);

	// Inspector(このフレームで先に描画済み)から報告された変更をキーとして取り込む。
	// 録画中の値変更と、フィールド右クリックのAdd Keyframeの両方がここへ届く。
	// 子孫Objectを選択して編集した場合は相対パスprefix("子/孫:")を付けてトラック名にする。
	for (const AnimationRecordedChange& change : ConsumeAnimationRecordedChanges()) {
		std::string fullPath = recordPathPrefix + change.path;
		if (!FindChannelValue(channels, fullPath)) {
			continue; // バインドできないチャンネルはトラック化しない。
		}
		AddKeyAtTime(*animator, fullPath, channels, previewTime_);
	}

	DrawToolbar(*scene, *animator, channels);
	ImGui::Separator();

	// 左: トラック一覧 / 右: ドープシート。
	ImGui::BeginChild("##TrackListPane", ImVec2(kTrackListWidth, 0.0f));
	// 先頭のAdd Trackボタンが右ペインのルーラーとほぼ同じ高さのヘッダーになる。
	DrawTrackList(*animator, channels);
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("##DopeSheetPane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
	if (viewMode_ == 0) {
		DrawDopeSheet(*animator, channels);
	} else {
		DrawCurveEditor(*animator, channels);
	}
	ImGui::EndChild();

	// --- キーボードショートカット(Ctrl+C=コピー / Ctrl+V=プレイヘッドへ貼り付け / Delete=選択キー削除) ---
	{
		ImGuiIO& io = ImGui::GetIO();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && !selectedKeys_.empty()) {
				CopySelectedKeys(animator->GetClip());
			}
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
				PasteCopiedKeys(animator->GetClip());
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !selectedKeys_.empty()) {
				DeleteSelectedKeys(animator->GetClip());
			}
		}
	}

	UpdatePreview(*animator);

	ImGui::End();

	// ウィンドウが[x]で閉じられたらプレビューを巻き戻す。
	if (pOpen && !*pOpen && previewing_) {
		EndPreview(*scene);
	}
}

void AnimationWindow::NotifyHidden() {
	SetAnimationKeyContextEnabled(false);
	if (recording_) {
		recording_ = false;
		SetAnimationRecording(false);
	}
	if (previewing_) {
		Scene* scene = EditorApplication::GetInstance()->GetCurrentScene();
		if (scene) {
			EndPreview(*scene);
		} else {
			previewing_ = false;
			previewPlaying_ = false;
			previewSnapshotJson_.clear();
			previewOwnerInstanceId_.clear();
		}
	}
}

#else // USE_IMGUI

void AnimationWindow::Draw(bool* pOpen) { (void)pOpen; }

void AnimationWindow::NotifyHidden() {}

#endif // USE_IMGUI

} // namespace KujataEngine
