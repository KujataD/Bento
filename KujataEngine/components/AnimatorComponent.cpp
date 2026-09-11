#include "AnimatorComponent.h"

#include "../base/Time.h"
#include "../runtime/AssetResolver.h"
#include "../runtime/InspectorUI.h"
#include "../runtime/PlayState.h"
#include "../scene/GameObject.h"
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace KujataEngine {

namespace {

std::string ReadString(const nlohmann::json& json, const char* key, const std::string& defaultValue) {
	if (!json.contains(key) || !json.at(key).is_string()) {
		return defaultValue;
	}
	return json.at(key).get<std::string>();
}

const std::string kEmptyString;

// objectPrefixは自身なら空、子孫なら"子/孫:"。子孫を再帰的に辿ってチャンネルを集める。
void CollectChannelsRecursive(GameObject& object, const std::string& objectPrefix, const Component* excludeComponent, std::vector<AnimatorChannel>& outChannels) {
	std::vector<AnimatableChannel> channels;
	for (const std::unique_ptr<Component>& component : object.GetComponents()) {
		if (!component || component.get() == excludeComponent) {
			continue;
		}
		channels.clear();
		component->CollectAnimatableChannels(channels);
		std::string prefix = objectPrefix + component->GetTypeName() + "/";
		for (const AnimatableChannel& channel : channels) {
			outChannels.push_back({prefix + channel.path, channel.value, channel.boolValue});
		}
	}

	for (GameObject* child : object.GetChildren()) {
		if (!child) {
			continue;
		}
		std::string childPrefix;
		if (objectPrefix.empty()) {
			childPrefix = child->GetName() + ":";
		} else {
			// "親:" → "親/子:" と伸ばす。
			childPrefix = objectPrefix.substr(0, objectPrefix.size() - 1) + "/" + child->GetName() + ":";
		}
		CollectChannelsRecursive(*child, childPrefix, excludeComponent, outChannels);
	}
}

// 向き基準移動チャンネル(Transform/moveForward等)とローカル回転チャンネル(Transform/localRotation.x等)の
// 適用先を再帰的に集める。pathの形式はCollectChannelsRecursiveと同一。
void CollectLocalMoveTargetsRecursive(
    GameObject& object,
    const std::string& objectPrefix,
    std::unordered_map<std::string, AnimatorComponent::LocalMoveTarget>& outMoveTargets,
    std::unordered_map<std::string, AnimatorComponent::LocalMoveTarget>& outRotationTargets) {
	std::string transformPrefix = objectPrefix + "Transform/";
	// **移動チャンネルは水平2軸だけ。** 上下(moveUp)は用意しない。
	// 加算で積まれたyは誰も戻さないため、少しでも残ると地面に埋まる/宙に浮くまま固定されてしまう。
	outMoveTargets.emplace(transformPrefix + "moveRight", AnimatorComponent::LocalMoveTarget{&object, 0});
	outMoveTargets.emplace(transformPrefix + "moveForward", AnimatorComponent::LocalMoveTarget{&object, 2});
	outRotationTargets.emplace(transformPrefix + "localRotation.x", AnimatorComponent::LocalMoveTarget{&object, 0});
	outRotationTargets.emplace(transformPrefix + "localRotation.y", AnimatorComponent::LocalMoveTarget{&object, 1});
	outRotationTargets.emplace(transformPrefix + "localRotation.z", AnimatorComponent::LocalMoveTarget{&object, 2});

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
		CollectLocalMoveTargetsRecursive(*child, childPrefix, outMoveTargets, outRotationTargets);
	}
}

// 自身+子孫の総Component数(バインディング再解決の変化検知用)。
size_t CountHierarchyComponents(GameObject& object) {
	size_t count = object.GetComponents().size();
	for (GameObject* child : object.GetChildren()) {
		if (child) {
			count += CountHierarchyComponents(*child);
		}
	}
	return count;
}

} // namespace

void AnimatorComponent::CollectHierarchyChannels(GameObject& root, const Component* excludeComponent, std::vector<AnimatorChannel>& outChannels) {
	CollectChannelsRecursive(root, "", excludeComponent, outChannels);
}

void AnimatorComponent::Update() {
	if (!IsGamePlaying() || !playing_ || !clipLoaded_) {
		return;
	}

	float duration = clip_.GetDuration();
	if (duration <= 0.0f) {
		return;
	}

	time_ += Time::GetDeltaTime() * speed_;

	// Loopの周回をまたいだら、加算トラックの基準値へ1周分の差分を積む(前進などが周回ごとに累積する)。
	if (clip_.wrapMode == AnimationWrapMode::Loop) {
		int cycle = static_cast<int>(time_ / duration);
		if (cycle != loopCycleCount_) {
			int cycleDelta = cycle - loopCycleCount_;
			loopCycleCount_ = cycle;
			for (const AnimationTrack& track : clip_.tracks) {
				float cycleValueDelta = (track.curve.Evaluate(duration) - track.curve.Evaluate(0.0f)) * static_cast<float>(cycleDelta);

				if (track.additive) {
					auto base = additiveBaseValues_.find(track.path);
					if (base != additiveBaseValues_.end()) {
						base->second += cycleValueDelta;
					}
				}

				// 向き基準移動チャンネルは、周回時にlast値を1周分戻して増分が連続するようにする
				// (これが無いと周回の瞬間に開始位置へ引き戻される)。
				if (localMoveTargets_.count(track.path) != 0) {
					auto last = localMoveLastValues_.find(track.path);
					if (last != localMoveLastValues_.end()) {
						last->second -= cycleValueDelta;
					}
				}

				// ローカル回転チャンネルも同様に、周回時の増分を連続させる。
				if (localRotationTargets_.count(track.path) != 0) {
					auto last = localRotationLastValues_.find(track.path);
					if (last != localRotationLastValues_.end()) {
						last->second -= cycleValueDelta;
					}
				}
			}
		}
	}

	bool finished = false;
	float evaluateTime = clip_.WrapTime(time_, finished);

	EvaluateAt(evaluateTime);

	if (finished) {
		// 再生終了: アニメーションが書き込んだ値を再生開始時の状態へ戻す(加算系は対象外)。
		playing_ = false;
		RestoreOriginalValues();
		RevertLocalRotations();
	}
}

void AnimatorComponent::OnPlayStart() {
	if (!clipLoaded_) {
		LoadClip();
	}
	ResolveBindings();
	ClearAdditiveBases();
	time_ = 0.0f;
	playing_ = playOnStart_ && clipLoaded_;
}

void AnimatorComponent::OnPlayStop() {
	playing_ = false;
	time_ = 0.0f;
	ClearAdditiveBases();
}

void AnimatorComponent::SetClipPath(const std::string& clipPath) {
	if (clipPath.empty()) {
		return;
	}

	IAssetResolver& assetDatabase = GetAssetResolver();
	std::filesystem::path resolvedPath = assetDatabase.ResolveAssetPath("", clipPath);
	if (resolvedPath.empty()) {
		return;
	}

	std::string assetId = assetDatabase.GetOrCreateAssetId(resolvedPath);
	std::string storedPath = assetDatabase.MakeProjectRelativePath(resolvedPath);
	if (storedPath.empty()) {
		storedPath = clipPath;
	}

	// 既にリストにあれば選択のみ、無ければ追加して選択する。
	for (int index = 0; index < static_cast<int>(clipReferences_.size()); ++index) {
		if (clipReferences_[index].path == storedPath || (!assetId.empty() && clipReferences_[index].assetId == assetId)) {
			SetCurrentClipIndex(index);
			return;
		}
	}

	clipReferences_.push_back({assetId, storedPath});
	SetCurrentClipIndex(static_cast<int>(clipReferences_.size()) - 1);
}

const std::string& AnimatorComponent::GetClipPath() const {
	if (currentClipIndex_ < 0 || currentClipIndex_ >= static_cast<int>(clipReferences_.size())) {
		return kEmptyString;
	}
	return clipReferences_[currentClipIndex_].path;
}

bool AnimatorComponent::SetCurrentClipIndex(int index) {
	if (index < 0 || index >= static_cast<int>(clipReferences_.size())) {
		return false;
	}
	currentClipIndex_ = index;
	time_ = 0.0f;
	// クリップ切替時は、前クリップが書き込んだ値とローカル回転を元へ戻してから基準を破棄する。
	// (これをしないと切替時点の途中ポーズが残り、次回の再生開始時に「元の状態」として誤記録される)
	RestoreOriginalValues();
	RevertLocalRotations();
	ClearAdditiveBases();
	return LoadClip();
}

void AnimatorComponent::RemoveClipAt(int index) {
	if (index < 0 || index >= static_cast<int>(clipReferences_.size())) {
		return;
	}
	clipReferences_.erase(clipReferences_.begin() + index);

	if (clipReferences_.empty()) {
		currentClipIndex_ = -1;
		clip_ = AnimationClipData{};
		clipLoaded_ = false;
		playing_ = false;
		return;
	}

	if (currentClipIndex_ >= static_cast<int>(clipReferences_.size())) {
		currentClipIndex_ = static_cast<int>(clipReferences_.size()) - 1;
	}
	LoadClip();
}

bool AnimatorComponent::PlayByName(const std::string& clipName) {
	IAssetResolver& assetDatabase = GetAssetResolver();
	for (int index = 0; index < static_cast<int>(clipReferences_.size()); ++index) {
		std::filesystem::path resolvedPath = assetDatabase.ResolveAssetPath(clipReferences_[index].assetId, clipReferences_[index].path);
		if (resolvedPath.empty()) {
			continue;
		}
		std::string message;
		AnimationClipData loaded;
		if (!AnimationClipAsset::Load(resolvedPath, loaded, message)) {
			continue;
		}
		if (loaded.name == clipName) {
			SetCurrentClipIndex(index);
			ResolveBindings();
			Play();
			return true;
		}
	}
	return false;
}

bool AnimatorComponent::SaveClip(std::string& message) const {
	if (!clipLoaded_ || GetClipPath().empty()) {
		message = "No clip to save.";
		return false;
	}

	IAssetResolver& assetDatabase = GetAssetResolver();
	const AnimationClipReference& reference = clipReferences_[currentClipIndex_];
	std::filesystem::path resolvedPath = assetDatabase.ResolveAssetPath(reference.assetId, reference.path);
	if (resolvedPath.empty()) {
		message = "Failed to resolve clip path: " + reference.path;
		return false;
	}

	return AnimationClipAsset::Save(resolvedPath, clip_, message);
}

void AnimatorComponent::Play() {
	if (!clipLoaded_) {
		LoadClip();
		ResolveBindings();
	}
	if (clipLoaded_) {
		time_ = 0.0f;
		// 前回再生の途中で残った書き込み値・ローカル回転を元へ戻してから基準を取り直す
		// (中断・切替時に前クリップの状態が残らないように)。
		RestoreOriginalValues();
		RevertLocalRotations();
		// 加算トラックは再生開始時点の値を基準にする(次の評価で遅延キャプチャ)。
		ClearAdditiveBases();
		playing_ = true;
	}
}

void AnimatorComponent::Stop() {
	playing_ = false;
	RestoreOriginalValues();
	RevertLocalRotations();
}

void AnimatorComponent::RestoreOriginalValues() {
	for (const auto& [path, baseValue] : restoreBaseValues_) {
		auto found = channelMap_.find(path);
		if (found != channelMap_.end() && found->second) {
			*found->second = baseValue;
		}
	}
	for (const auto& [path, baseValue] : restoreBaseBoolValues_) {
		auto found = boolChannelMap_.find(path);
		if (found != boolChannelMap_.end() && found->second) {
			*found->second = baseValue;
		}
	}
	restoreBaseValues_.clear();
	restoreBaseBoolValues_.clear();
}

void AnimatorComponent::RevertLocalRotations() {
	for (const auto& [path, lastValue] : localRotationLastValues_) {
		auto base = localRotationBaseValues_.find(path);
		auto target = localRotationTargets_.find(path);
		if (base == localRotationBaseValues_.end() || target == localRotationTargets_.end() || !target->second.object) {
			continue;
		}

		// 適用済みの正味回転 = 最終評価値 - 初回基準値。逆回転で打ち消す。
		float appliedTotal = lastValue - base->second;
		if (std::abs(appliedTotal) < 1.0e-7f) {
			continue;
		}

		Vector3 localAxis = {0.0f, 0.0f, 0.0f};
		if (target->second.axis == 0) {
			localAxis.x = 1.0f;
		} else if (target->second.axis == 1) {
			localAxis.y = 1.0f;
		} else {
			localAxis.z = 1.0f;
		}

		WorldTransform& transform = target->second.object->GetTransform();
		Quaternion undoRotation = Quaternion::MakeRotateAxisAngle(localAxis, -appliedTotal);
		transform.SetRotationFromQuaternion((transform.GetRotationQuaternion() * undoRotation).Normalized());
	}

	localRotationLastValues_.clear();
	localRotationBaseValues_.clear();
}

void AnimatorComponent::EvaluateAt(float time) {
	// 自身+子孫のComponent構成が変わったらチャンネルの解決をやり直す(追加/削除/子の増減を検知)。
	GameObject* owner = GetOwner();
	if (owner && CountHierarchyComponents(*owner) != resolvedComponentCount_) {
		bindingsDirty_ = true;
	}
	if (bindingsDirty_) {
		ResolveBindings();
	}

	// カーブは毎回clip_のトラックから直接参照する(トラック増減でvectorが再確保されるため、
	// カーブへのポインタをキャッシュしてはならない)。
	for (const AnimationTrack& track : clip_.tracks) {
		auto found = channelMap_.find(track.path);
		if (found == channelMap_.end() || !found->second) {
			continue;
		}

		float value = track.curve.Evaluate(time);
		if (track.additive) {
			// 加算モード: 初回評価時の現在値を基準として遅延キャプチャし、カーブ値を差分として足す。
			auto base = additiveBaseValues_.find(track.path);
			if (base == additiveBaseValues_.end()) {
				base = additiveBaseValues_.emplace(track.path, *found->second).first;
			}
			value += base->second;
		} else {
			// 通常トラック: 最初に書き込む直前の値を記憶しておき、再生終了時に元へ戻す。
			restoreBaseValues_.try_emplace(track.path, *found->second);
		}
		*found->second = value;
	}

	// --- bool型チャンネル ---
	// カーブ値を補間して閾値0.5で0/1に量子化する。エルミート補間で完全に1.0へ届かなくても、
	// 0.5を超えていればtrueになるため、攻撃判定などのON/OFFが確実に切り替わる。
	for (const AnimationTrack& track : clip_.tracks) {
		auto found = boolChannelMap_.find(track.path);
		if (found == boolChannelMap_.end() || !found->second) {
			continue;
		}
		// 最初に書き込む直前の値を記憶しておき、再生終了時に元へ戻す(攻撃判定の消し忘れ防止にもなる)。
		restoreBaseBoolValues_.try_emplace(track.path, *found->second);
		*found->second = track.curve.Evaluate(time) >= 0.5f;
	}

	// --- 向き基準の移動チャンネル(moveForward/moveRight) ---
	// カーブ値の「前回評価からの増分」を、その時点の自身の向きで回してtranslationへ加算する。
	// 移動中に旋回すると軌道が曲がる(ルートモーション風)。
	for (const AnimationTrack& track : clip_.tracks) {
		auto target = localMoveTargets_.find(track.path);
		if (target == localMoveTargets_.end() || !target->second.object) {
			continue;
		}

		float value = track.curve.Evaluate(time);
		auto last = localMoveLastValues_.find(track.path);
		if (last == localMoveLastValues_.end()) {
			// 初回は基準を取るだけ(評価開始時点の値からの増分にする)。
			localMoveLastValues_.emplace(track.path, value);
			continue;
		}
		float delta = value - last->second;
		last->second = value;
		if (std::abs(delta) < 1.0e-7f) {
			continue;
		}

		Vector3 localAxis = {0.0f, 0.0f, 0.0f};
		if (target->second.axis == 0) {
			localAxis.x = 1.0f;
		} else {
			localAxis.z = 1.0f;
		}

		WorldTransform& transform = target->second.object->GetTransform();
		Vector3 moveDelta = localAxis * delta;

		// **自分の回転では回さない。**
		// translation_は親から見たローカル座標なので、キャラの向き(=親の回転)はワールド行列の
		// 合成時に自動で掛かる。ここで自分の回転まで掛けると、前傾や腰のひねりといった
		// 「ポーズ」に踏み込みが引きずられ、斜め下へ進んで地面に潜る/横へずれる。
		// 親がいない(Animatorが最上位に付く旧構成)ときだけ、自分の回転が向きそのものなので回す。
		if (!transform.parent_) {
			moveDelta = transform.GetRotationQuaternion().RotateVector(moveDelta);
		}

		// **上下成分は常に捨てる。**
		// 加算で積まれたyは誰も戻さないので、わずかでも残ると地面に埋まる/宙に浮くまま固定される。
		moveDelta.y = 0.0f;
		transform.translation_ += moveDelta;
	}

	// --- ローカル回転チャンネル(localRotation.x/.y/.z) ---
	// カーブ値[rad]の「前回評価からの増分」を、今向いている方向を基準としたローカル軸回転として
	// 現在の姿勢へ合成する(のけぞり等、向きに依存しない相対回転に使う)。
	// 規約: 「aを適用してからb」= b*a なので、ローカル軸の追加回転は q * Δq(Δqを先に適用)。
	for (const AnimationTrack& track : clip_.tracks) {
		auto target = localRotationTargets_.find(track.path);
		if (target == localRotationTargets_.end() || !target->second.object) {
			continue;
		}

		float value = track.curve.Evaluate(time);
		auto last = localRotationLastValues_.find(track.path);
		if (last == localRotationLastValues_.end()) {
			// 初回は基準を取るだけ(評価開始時点の値からの増分にする)。基準値は中断時の巻き戻しにも使う。
			localRotationLastValues_.emplace(track.path, value);
			localRotationBaseValues_.emplace(track.path, value);
			continue;
		}
		float delta = value - last->second;
		last->second = value;
		if (std::abs(delta) < 1.0e-7f) {
			continue;
		}

		Vector3 localAxis = {0.0f, 0.0f, 0.0f};
		if (target->second.axis == 0) {
			localAxis.x = 1.0f;
		} else if (target->second.axis == 1) {
			localAxis.y = 1.0f;
		} else {
			localAxis.z = 1.0f;
		}

		WorldTransform& transform = target->second.object->GetTransform();
		Quaternion deltaRotation = Quaternion::MakeRotateAxisAngle(localAxis, delta);
		transform.SetRotationFromQuaternion((transform.GetRotationQuaternion() * deltaRotation).Normalized());
	}
}

void AnimatorComponent::ResolveBindings() {
	channelMap_.clear();
	boolChannelMap_.clear();
	bindingsDirty_ = false;
	resolvedComponentCount_ = 0;

	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}
	resolvedComponentCount_ = CountHierarchyComponents(*owner);

	// 自身と子孫全てのチャンネル("[子/孫:]ComponentTypeName/チャンネル名") → float*/bool* の対応表を作る。
	// 同種Component/同名の子が複数ある場合は最初の1つを対象にする(try_emplace)。
	std::vector<AnimatorChannel> channels;
	CollectHierarchyChannels(*owner, this, channels);
	for (const AnimatorChannel& channel : channels) {
		if (channel.boolValue) {
			boolChannelMap_.try_emplace(channel.path, channel.boolValue);
		} else if (channel.value) {
			channelMap_.try_emplace(channel.path, channel.value);
		}
	}

	// 向き基準移動/ローカル回転チャンネルの適用先(GameObject+軸)も解決し直す。
	localMoveTargets_.clear();
	localRotationTargets_.clear();
	CollectLocalMoveTargetsRecursive(*owner, "", localMoveTargets_, localRotationTargets_);
}

bool AnimatorComponent::LoadClip() {
	clipLoaded_ = false;
	clip_ = AnimationClipData{};
	bindingsDirty_ = true;

	if (currentClipIndex_ < 0 || currentClipIndex_ >= static_cast<int>(clipReferences_.size())) {
		return false;
	}

	const AnimationClipReference& reference = clipReferences_[currentClipIndex_];
	IAssetResolver& assetDatabase = GetAssetResolver();
	std::filesystem::path resolvedPath = assetDatabase.ResolveAssetPath(reference.assetId, reference.path);
	if (resolvedPath.empty()) {
		return false;
	}

	std::string message;
	AnimationClipData loaded;
	if (!AnimationClipAsset::Load(resolvedPath, loaded, message)) {
		return false;
	}

	clip_ = std::move(loaded);
	clipLoaded_ = true;
	return true;
}

void AnimatorComponent::DrawInspector() {
#ifdef USE_IMGUI
	// クリップ一覧(選択中に*印)。行クリックで切替。
	if (clipReferences_.empty()) {
		InspectorUI::TextDisabled("No clips. (Animation WindowかProjectからD&Dで追加)");
	} else {
		InspectorUI::TextUnformatted("Clips:");
		int removeIndex = -1;
		for (int index = 0; index < static_cast<int>(clipReferences_.size()); ++index) {
			std::string label = (index == currentClipIndex_ ? "* " : "  ") + clipReferences_[index].path;
			InspectorUI::TextUnformatted(label.c_str());
			InspectorUI::SameLine();
			std::string selectLabel = "Select##clip" + std::to_string(index);
			if (index != currentClipIndex_ && InspectorUI::Button(selectLabel.c_str())) {
				SetCurrentClipIndex(index);
			}
			if (index == currentClipIndex_) {
				InspectorUI::TextDisabled("(current)");
			}
			InspectorUI::SameLine();
			std::string removeLabel = "X##clip" + std::to_string(index);
			if (InspectorUI::Button(removeLabel.c_str())) {
				removeIndex = index;
			}
		}
		if (removeIndex >= 0) {
			RemoveClipAt(removeIndex);
		}
	}

	// パス直接入力でのクリップ追加(D&Dの代替手段)。
	std::array<char, 256> pathBuffer{};
	if (InspectorUI::InputText("Add Clip Path", pathBuffer.data(), pathBuffer.size())) {
		if (pathBuffer[0] != '\0') {
			SetClipPath(pathBuffer.data());
		}
	}

	if (clipLoaded_) {
		std::string info = "Clip: " + clip_.name + "  Tracks: " + std::to_string(clip_.tracks.size()) + "  Duration: " + std::to_string(clip_.GetDuration()) + "  Wrap: " + AnimationClipAsset::ToString(clip_.wrapMode);
		InspectorUI::TextDisabled(info.c_str());
	}

	InspectorUI::Checkbox("Play On Start", &playOnStart_);
	InspectorUI::DragFloat("Speed", &speed_, 0.01f);

	if (InspectorUI::Button("Reload Clip")) {
		LoadClip();
		ResolveBindings();
	}
#endif // USE_IMGUI
}

void AnimatorComponent::WriteJson(nlohmann::json& json) const {
	nlohmann::json clipsJson = nlohmann::json::array();
	for (const AnimationClipReference& reference : clipReferences_) {
		nlohmann::json clipJson;
		clipJson["assetId"] = reference.assetId;
		clipJson["path"] = reference.path;
		clipsJson.push_back(clipJson);
	}
	json["clips"] = clipsJson;
	json["currentClip"] = currentClipIndex_;
	json["playOnStart"] = playOnStart_;
	json["speed"] = speed_;
}

void AnimatorComponent::ReadJson(const nlohmann::json& json) {
	clipReferences_.clear();
	currentClipIndex_ = -1;

	if (json.contains("clips") && json.at("clips").is_array()) {
		for (const nlohmann::json& clipJson : json.at("clips")) {
			if (!clipJson.is_object()) {
				continue;
			}
			AnimationClipReference reference;
			reference.assetId = ReadString(clipJson, "assetId", "");
			reference.path = ReadString(clipJson, "path", "");
			if (!reference.assetId.empty() || !reference.path.empty()) {
				clipReferences_.push_back(std::move(reference));
			}
		}
		if (json.contains("currentClip") && json.at("currentClip").is_number_integer()) {
			currentClipIndex_ = json.at("currentClip").get<int>();
		}
	} else {
		// 旧形式(単一clipAssetId/clipPath)からの互換読み込み。
		AnimationClipReference reference;
		reference.assetId = ReadString(json, "clipAssetId", "");
		reference.path = ReadString(json, "clipPath", "");
		if (!reference.assetId.empty() || !reference.path.empty()) {
			clipReferences_.push_back(std::move(reference));
			currentClipIndex_ = 0;
		}
	}

	if (currentClipIndex_ < 0 || currentClipIndex_ >= static_cast<int>(clipReferences_.size())) {
		currentClipIndex_ = clipReferences_.empty() ? -1 : 0;
	}

	if (json.contains("playOnStart") && json.at("playOnStart").is_boolean()) {
		playOnStart_ = json.at("playOnStart").get<bool>();
	}
	if (json.contains("speed") && json.at("speed").is_number()) {
		speed_ = json.at("speed").get<float>();
	}

	LoadClip();
	bindingsDirty_ = true;
}

} // namespace KujataEngine
