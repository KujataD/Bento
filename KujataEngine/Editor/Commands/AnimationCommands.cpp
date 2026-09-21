// エディタの CUI のコマンド: アニメーション(処理は AnimationEditing。Animation ウィンドウと同じもの)。
// 設計と一覧は .claude/editor-automation.md。共通の関数は EditorCommandUtil にある。
#include "EditorCommandUtil.h"
#include "../AnimationEditing.h"
#include "../EditorConsole.h"
#include "../../assets/AnimationClipAsset.h"
#include "../../components/AnimatorComponent.h"
#include "../../scene/GameObject.h"
#include "../../scene/Scene.h"
#include <filesystem>
#include <string>

namespace KujataEngine {

namespace {

using namespace EditorCommandUtil;
using nlohmann::json;

AnimatorComponent* ResolveAnimator(Scene& scene, const std::string& spec, std::string& error, bool requireClip) {
	GameObject* gameObject = ResolveObject(scene, spec, error);
	if (!gameObject) {
		return nullptr;
	}
	AnimatorComponent* animator = gameObject->GetComponent<AnimatorComponent>();
	if (!animator) {
		error = MakeObjectPath(gameObject) + " に AnimatorComponent がありません(component.add で足せます)。";
		return nullptr;
	}
	if (requireClip && !animator->HasClip()) {
		error = MakeObjectPath(gameObject) + " の Animator にクリップがありません(animation.createClip で作れます)。";
		return nullptr;
	}
	return animator;
}

EditorCommandResult CommandAnimationInfo(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	AnimatorComponent* animator = ResolveAnimator(*scene, args.Get(0), error, false);
	if (!animator) {
		return EditorCommandResult::Failure(error);
	}

	json result;
	json clips = json::array();
	for (const AnimationClipReference& reference : animator->GetClipReferences()) {
		clips.push_back(reference.path);
	}
	result["clips"] = clips;
	result["currentClip"] = animator->GetCurrentClipIndex();
	result["hasClip"] = animator->HasClip();
	result["playing"] = animator->IsPlaying();
	result["time"] = animator->GetTime();
	if (animator->HasClip()) {
		const AnimationClipData& clip = animator->GetClip();
		result["name"] = clip.name;
		result["wrapMode"] = AnimationClipAsset::ToString(clip.wrapMode);
		result["duration"] = clip.GetDuration();
		json tracks = json::array();
		for (const AnimationTrack& track : clip.tracks) {
			json keys = json::array();
			for (const AnimationKeyframe& key : track.curve.keys) {
				keys.push_back({{"time", key.time}, {"value", key.value}, {"easing", AnimationClipAsset::ToString(key.easing)}});
			}
			tracks.push_back({{"path", track.path}, {"additive", track.additive}, {"keys", keys}});
		}
		result["tracks"] = tracks;
	}
	return EditorCommandResult::Success(result);
}

EditorCommandResult CommandAnimationChannels(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	AnimatorComponent* animator = ResolveAnimator(*scene, args.Get(0), error, false);
	if (!animator) {
		return EditorCommandResult::Failure(error);
	}
	return EditorCommandResult::Success(AnimationEditing::ListChannels(*animator));
}

EditorCommandResult CommandAnimationCreateClip(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	AnimatorComponent* animator = ResolveAnimator(*scene, args.Get(0), error, false);
	if (!animator) {
		return EditorCommandResult::Failure(error);
	}
	if (args.Count() < 2) {
		return EditorCommandResult::Failure("クリップの名前を指定してください(例: animation.createClip Door Open)。");
	}
	std::filesystem::path clipPath;
	std::string message;
	if (!AnimationEditing::CreateClip(*animator, args.Get(1), clipPath, message)) {
		return EditorCommandResult::Failure(message);
	}
	EditorConsole::GetInstance()->AddLog("[Animation] " + message, EditorLogLevel::Info);
	return EditorCommandResult::Success(clipPath.generic_string());
}

EditorCommandResult CommandAnimationAddKey(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	if (args.Count() < 3) {
		return EditorCommandResult::Failure("使い方: animation.addKey <オブジェクト> <トラック> <時刻(秒)> [値](トラックは animation.channels で一覧)");
	}
	std::string error;
	AnimatorComponent* animator = ResolveAnimator(*scene, args.Get(0), error, true);
	if (!animator) {
		return EditorCommandResult::Failure(error);
	}
	const std::string& trackPath = args.Get(1);
	float time = 0.0f;
	if (!ParseFloat(args.Get(2), time) || time < 0.0f) {
		return EditorCommandResult::Failure("時刻は 0 以上の秒数で指定してください(例: 0.5)。");
	}

	// 値を指定しなければ、そのチャンネルの今の値でキーを打つ(Animation ウィンドウの Add Key と同じ)。
	float* channelValue = AnimationEditing::FindChannelValue(*animator, trackPath);
	float explicitValue = 0.0f;
	const float* keyValue = channelValue;
	if (args.Count() > 3) {
		if (!ParseFloat(args.Get(3), explicitValue)) {
			return EditorCommandResult::Failure("値は数値で指定してください(例: 1.5)。");
		}
		keyValue = &explicitValue;
	} else if (!channelValue && !animator->GetClip().FindTrack(trackPath)) {
		// 値もなく、チャンネルも既存トラックもない = たぶんトラック名の打ち間違い。
		return EditorCommandResult::Failure("トラックが見つかりません: " + trackPath + "(animation.channels で一覧を表示できます)");
	}

	const int keyIndex = AnimationEditing::AddKey(*animator, trackPath, time, keyValue);
	const AnimationKeyframe& key = animator->GetClip().FindTrack(trackPath)->curve.keys[keyIndex];
	json result;
	result["track"] = trackPath;
	result["time"] = key.time;
	result["value"] = key.value;
	result["note"] = "クリップのメモリ上だけの変更です。animation.save で保存します(シーンの Undo では戻りません)。";
	return EditorCommandResult::Success(result);
}

EditorCommandResult CommandAnimationRemoveKey(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	AnimatorComponent* animator = ResolveAnimator(*scene, args.Get(0), error, true);
	if (!animator) {
		return EditorCommandResult::Failure(error);
	}
	float time = 0.0f;
	if (args.Count() < 3 || !ParseFloat(args.Get(2), time)) {
		return EditorCommandResult::Failure("使い方: animation.removeKey <オブジェクト> <トラック> <時刻(秒)>");
	}
	// 表示や入力の丸めで少しずれても消せるよう、1/1000 秒の誤差は同じ時刻とみなす。
	if (!AnimationEditing::RemoveKey(*animator, args.Get(1), time, 0.001f)) {
		return EditorCommandResult::Failure("その時刻のキーがありません: " + args.Get(1) + " @ " + args.Get(2) + "(animation.info でキーを確認できます)");
	}
	return EditorCommandResult::Success();
}

EditorCommandResult CommandAnimationSave(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	AnimatorComponent* animator = ResolveAnimator(*scene, args.Get(0), error, true);
	if (!animator) {
		return EditorCommandResult::Failure(error);
	}
	std::string message;
	if (!animator->SaveClip(message)) {
		return EditorCommandResult::Failure("クリップを保存できませんでした: " + message);
	}
	EditorConsole::GetInstance()->AddLog("[Animation] Saved: " + animator->GetClipPath(), EditorLogLevel::Info);
	return EditorCommandResult::Success(animator->GetClipPath());
}

} // namespace

void RegisterAnimationCommands(EditorCommandRegistry& registry) {
	registry.Register("animation.info", "animation.info <オブジェクト>", "Animator のクリップ・トラック・キーを表示する", CommandAnimationInfo);
	registry.Register("animation.channels", "animation.channels <オブジェクト>", "キーを打てるトラック(チャンネル)の一覧", CommandAnimationChannels);
	registry.Register("animation.createClip", "animation.createClip <オブジェクト> <名前>", "新しいクリップを Data/Animations/ に作って Animator に持たせる", CommandAnimationCreateClip);
	registry.Register("animation.addKey", "animation.addKey <オブジェクト> <トラック> <時刻(秒)> [値]",
	                  "キーを打つ(値を省略すると今の値)。メモリ上の変更なので animation.save で保存する", CommandAnimationAddKey);
	registry.Register("animation.removeKey", "animation.removeKey <オブジェクト> <トラック> <時刻(秒)>", "キーを消す", CommandAnimationRemoveKey);
	registry.Register("animation.save", "animation.save <オブジェクト>", "クリップをファイルへ保存する(Animation ウィンドウの Save Clip と同じ)", CommandAnimationSave);
}

} // namespace KujataEngine
