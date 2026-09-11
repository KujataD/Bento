#pragma once

#include <KujataEngine.h>
#include <string>

/// <summary>
/// **Play開始時にPrefabを1つ置くだけの器。** 自分の位置・向きがそのまま生成物の姿勢になる。
///
/// ボスのように階層の深いPrefabをシーンへ置くと、シーンJSONは**完全に展開**されて
/// GameObjectファイルが100個近く増える。中身は元のPrefabと同じなので、
/// 「シーンには置き場所だけを残し、実体はPlayで作る」ほうが管理が楽になる
/// (レティクル・衝撃刃・蘇生ゲージと同じ流儀を、ボス本体にも広げただけ)。
///
/// 生成物はPlayインスタンス内に作られるので、停止すれば一緒に消える。
/// </summary>
class PrefabSpawner : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "PrefabSpawner"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void OnPlayStop() override;
	void Update() override;

	/// <summary>生成したオブジェクト(まだ作っていなければnullptr)。</summary>
	KujataEngine::GameObject* GetSpawned() const { return spawned_; }

private:
	/// <summary>
	/// 生成物とその子孫へ Initialize / OnPlayStart を配る。
	/// **Prefabから作った物には engine 側からこれらが届かない**ため、ここで肩代わりする。
	/// </summary>
	void BeginPlayRecursive(KujataEngine::GameObject* object);

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(prefabPath_, "Prefab", "置くPrefabのパス(プロジェクトルート相対)。空なら何もしない。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(useOwnerTransform_, "Use Owner Transform",
		    "自分の位置・向き・スケールを生成物へ移すか。OFFならPrefab側の値のまま置く。");
		KUJATA_REGISTER_STRING_NAMED_TIP(overrideName_, "Override Name",
		    "生成物の名前を上書きする(空ならPrefabの名前のまま)。\n"
		    "**GameFlowManagerのBoss Nameなど、名前で探す相手に合わせるため**に使う。");
	}

	// 置くPrefab。
	KUJATA_FIELD_STRING(prefabPath_, "");
	// 自分の姿勢を移すか。
	KUJATA_FIELD_BOOL(useOwnerTransform_, true);
	// 名前の上書き。
	KUJATA_FIELD_STRING(overrideName_, "");

	// 生成物(Playインスタンス内なので停止で消える)。
	KujataEngine::GameObject* spawned_ = nullptr;
	// 次のUpdateで生成する予定か(OnPlayStartでは作れないため1フレーム遅らせる)。
	bool pending_ = false;
};
