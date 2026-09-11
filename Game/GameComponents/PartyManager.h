#pragma once
#include <KujataEngine.h>
#include <string>

/// <summary>
/// パーティ(プレイアブル2人)の役割を一元管理するシーン常駐Component。
/// 「誰を操作するか(リーダー)」を決め、Play開始時に以下を行う:
///   - リーダー: 入力頭脳(Player)を有効化、AI頭脳(AllyAIBrain)を無効化
///   - 味方NPC: 入力頭脳を無効化、AI頭脳を有効化(エルデンリングの霊灰のような参戦)
///   - カメラ(OrbitCameraComponent)の追従先をリーダーへ向ける
///
/// 実行中のキャラ切替(十字キー下/Tab)もここで受ける: SwapLeader()で役割を入れ替え、
/// 同じ手順でもう一度適用する。HP/スタミナバーやZ注目はFindLeaderInSceneで「今のリーダー」を
/// 毎フレーム引くので、切替に自動で追従する。
///
/// キャラの体(CharacterMotor)は頭脳がどちらでも同じAPIで動くため、
/// リーダーを入れ替えても各キャラの挙動コードは変わらない。
/// </summary>
class PartyManager : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "PartyManager"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	/// <summary>現在のリーダー(操作キャラ)。見つからなければnullptr。</summary>
	KujataEngine::GameObject* GetLeader() const { return leader_; }

	/// <summary>味方NPC側のキャラ。見つからなければnullptr。</summary>
	KujataEngine::GameObject* GetAlly() const { return ally_; }

	/// <summary>2人ともAIに任せているか(勝率計測用のモード)。</summary>
	bool IsAutoBattle() const { return autoBattle_; }

	/// <summary>
	/// リーダーと味方を入れ替える(実行中のキャラ切替)。
	/// 相方が不在/死亡、どちらかが硬直中、切替クールダウン中は何もせずfalse。
	/// </summary>
	bool SwapLeader();

	/// <summary>シーン内のPartyManagerを探す(HP/スタミナバーなど「今のリーダー」を知りたい側が使う)。</summary>
	static PartyManager* FindInScene(KujataEngine::Scene* scene);

	/// <summary>
	/// シーン内のPartyManagerが指す現在のリーダーを返す。PartyManagerが無ければfallbackNameで名前検索する
	/// (PartyManagerを置いていないシーンでも従来どおり動かすため)。
	/// </summary>
	static KujataEngine::GameObject* FindLeaderInScene(KujataEngine::Scene* scene, const std::string& fallbackName);

private:
	/// <summary>
	/// リーダーと味方の役割を実際に入れ替える(可否判定はしない)。
	/// SwapLeader(プレイヤー操作)と「リーダーが倒れたときの自動移乗」の共通処理。
	/// </summary>
	void PerformSwap();

	/// <summary>リーダーが倒れていて相方が健在なら、操作を相方へ移す。</summary>
	void SwapIfLeaderIsDown();

	/// <summary>両キャラの頭脳の有効/無効とカメラ追従先を、現在のリーダー設定に合わせて適用する。</summary>
	void ApplyRoles();
	/// <summary>characterの頭脳Componentを切り替える(isLeader=trueなら入力、falseならAI)。</summary>
	void SetBrainMode(KujataEngine::GameObject* character, bool isLeader);

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED(leaderName_, "Leader Name");
		KUJATA_REGISTER_STRING_NAMED(allyName_, "Ally Name");
		KUJATA_REGISTER_STRING_NAMED(cameraName_, "Camera Name");
		KUJATA_REGISTER_BOOL_NAMED_TIP(swapEnabled_, "Swap Enabled",
		    "実行中のキャラ切替(十字キー下/Tab)を許可するか。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(autoBattle_, "Auto Battle",
		    "**2人ともAIに任せる(検証用)。** 入力頭脳(Player)を両方無効にし、AI頭脳を両方有効にする。\n"
		    "キャラ切替も止まる。カメラはリーダー側を追い続けるので観戦できる。\n"
		    "勝率の計測に使うためのもので、**通常プレイでは必ずOFF**にしておくこと。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(swapCooldown_, "Swap Cooldown", 0.05f, 0.0f, 10.0f,
		    "切替後、次に切り替えられるまでの秒数(連打での往復を防ぐ)。");
	}

	// 操作するキャラのGameObject名。
	KUJATA_FIELD_STRING(leaderName_, "Pawn");
	// 味方NPCとして参戦するキャラのGameObject名。
	KUJATA_FIELD_STRING(allyName_, "Bishop");
	// 追従カメラ(OrbitCameraComponent持ち)のGameObject名。
	KUJATA_FIELD_STRING(cameraName_, "Main Camera");
	// 実行中切替の許可。
	KUJATA_FIELD_BOOL(swapEnabled_, true);
	// 切替クールダウン[s]。
	KUJATA_FIELD_FLOAT(swapCooldown_, 1.0f);
	// 2人ともAIに任せる(勝率計測用)。
	KUJATA_FIELD_BOOL(autoBattle_, false);

	KujataEngine::GameObject* leader_ = nullptr;
	KujataEngine::GameObject* ally_ = nullptr;

	// 切替クールダウンの残り[s]。
	float swapCooldownTimer_ = 0.0f;
};
