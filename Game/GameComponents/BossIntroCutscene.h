#pragma once

#include <KujataEngine.h>
#include <string>
#include <vector>

namespace KujataEngine {
class OrbitCameraComponent;
class TextComponent;
}

/// <summary>
/// **ボス戦の幕開けの演出。** ボス戦シーンに1つ置く(GameFlowと同じGameObjectでよい)。
///
/// 拍の組み立て:
///   1. 見上げる   … ボスの足元から見上げ、ゆっくり回り込みながら引いていく。**大きさを先に見せる**
///   2. 名を出す   … 引ききった画のまま、ボスの名前が浮かぶ
///   3. 戻る       … 二人の背後へカメラが降りてくる。**ここでボスが正面奥に収まる**
///   4. 手放す     … カメラを返し、ボスのHPバーを出して操作を解禁する
///
/// **舞台はボスが奥・二人が手前。** カメラのyawは配置時の `rotation.y` から始まるので
/// ([[OrbitCameraComponent]]::OnPlayStart)、シーン側で二人をボスより手前(-Z)へ置き、
/// カメラのyawを0にしておけば、演出が明けた瞬間からボスが正面に居る。
///
/// 演出中は**ボスも二人も止める**。ボスは `SetSuspended`、二人は頭脳(Player / AllyAIBrain)を
/// 一時的に無効化して、**元の有効/無効をそのまま覚えてから戻す**
/// (どちらが操作キャラかは [[PartyManager]] が決めているので、こちらで決め直さない)。
///
/// 時間は必ずUnscaledで数える。ヒットストップやポーズと無関係に進めたいため。
/// </summary>
class BossIntroCutscene : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "BossIntroCutscene"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void OnPlayStop() override;
	void Update() override;

	/// <summary>
	/// 演出中か。**この間 [[GameFlowManager]] は戦闘時計を進めず、[[PauseMenu]] は開かない。**
	/// </summary>
	bool IsPlaying() const { return phase_ != Phase::Done; }

	/// <summary>シーン内のBossIntroCutsceneを探す(無ければnullptr)。</summary>
	static BossIntroCutscene* FindInScene(KujataEngine::Scene* scene);

	/// <summary>そのシーンで開幕演出が流れている最中か(置かれていないシーンではfalse)。</summary>
	static bool IsSceneIntroPlaying(KujataEngine::Scene* scene);

private:
	enum class Phase {
		Reveal,   // 1. 見上げる
		Name,     // 2. 名を出す
		Approach, // 3. 二人の背後へ戻る
		Done,     // 4. 手放した後(以降は何もしない)
	};

	/// <summary>操作とAIを止める / 戻す。**止めた時点の有効状態を覚えてから戻す。**</summary>
	void SuspendActors(bool suspend);
	/// <summary>カメラへ今フレームの画を渡す。</summary>
	void ApplyShot(const KujataEngine::Vector3& position, const KujataEngine::Vector3& lookAt);
	/// <summary>1. 見上げる画。progressは0→1。</summary>
	void UpdateRevealShot(float progress);
	/// <summary>3. 二人の背後へ降りる画。progressは0→1。</summary>
	void UpdateApproachShot(float progress);
	/// <summary>演出を畳んでカメラと操作を返す。</summary>
	void Finish();

	/// <summary>ボスの位置(見つからなければ原点)。</summary>
	KujataEngine::Vector3 BossPosition();
	/// <summary>操作キャラの位置(見つからなければ原点)。</summary>
	KujataEngine::Vector3 LeaderPosition();
	/// <summary>ボスから見た「二人の居る方向」の水平単位ベクトル。</summary>
	KujataEngine::Vector3 FrontDirection();

	KujataEngine::GameObject* FindBoss();
	KujataEngine::OrbitCameraComponent* FindCamera();
	/// <summary>名前を出すTextを掴む(無ければnullptr)。</summary>
	KujataEngine::TextComponent* FindNameText();
	/// <summary>名前の帯と文字のαをまとめて動かす。</summary>
	void ApplyNameAlpha(float alpha);
	/// <summary>名前でGameObjectの表示/非表示を切り替える。</summary>
	void SetObjectActive(const std::string& name, bool active);

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(bossName_, "Boss Name", "見せるボスのGameObject名。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(bossLookHeight_, "Boss Look Height", 0.1f, 0.0f, 40.0f,
		    "ボスのどの高さを見るか(足元からの高さ)。**大きいボスほど上げる。**");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(revealSeconds_, "Reveal Seconds", 0.05f, 0.0f, 12.0f, "1. 見上げてから引ききるまでの秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(revealStartDistance_, "Reveal Start Distance", 0.1f, 1.0f, 80.0f,
		    "見上げ始めの水平距離。**近いほど「見上げている」画になる。**");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(revealEndDistance_, "Reveal End Distance", 0.1f, 1.0f, 120.0f, "引ききったときの水平距離。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(revealStartHeight_, "Reveal Start Height", 0.1f, -5.0f, 40.0f, "見上げ始めのカメラの高さ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(revealEndHeight_, "Reveal End Height", 0.1f, -5.0f, 60.0f, "引ききったときのカメラの高さ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(revealOrbitDeg_, "Reveal Orbit", 1.0f, -180.0f, 180.0f,
		    "見上げている間に回り込む角度[度]。**0にすると止まった絵になる。**");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(nameSeconds_, "Name Seconds", 0.05f, 0.0f, 8.0f, "2. 名前を出しておく秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(nameFadeSeconds_, "Name Fade", 0.05f, 0.0f, 4.0f, "名前が浮かび、消えるまでの秒数(片側)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(approachSeconds_, "Approach Seconds", 0.05f, 0.0f, 8.0f, "3. 二人の背後へ降りるまでの秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(followDistance_, "Follow Distance", 0.1f, 1.0f, 40.0f,
		    "着地点(通常視点)の引き。**OrbitCameraのDistanceに合わせる**と繋ぎ目が出ない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(followHeight_, "Follow Height", 0.1f, 0.0f, 20.0f, "着地点のカメラの高さ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(pivotHeight_, "Pivot Height", 0.1f, 0.0f, 10.0f, "着地点で見る高さ(キャラの足元からの高さ)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(blendSpeed_, "Blend Speed", 0.1f, 0.5f, 40.0f,
		    "カメラが指示へ寄る速さ[1/s]。小さいほどゆっくり追いつく。");
		KUJATA_REGISTER_STRING_NAMED_TIP(nameObjectName_, "Name Object",
		    "ボスの名前を出すCanvasのGameObject名。空なら名前は出さず、その拍ぶんの間だけ取る。");
		KUJATA_REGISTER_STRING_NAMED_TIP(nameTextName_, "Name Text", "名前のTextのGameObject名(αを動かす先)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(nameBgName_, "Name Backdrop",
		    "名前の下に敷く帯のGameObject名。**文字と一緒にαを動かす**ので、\n"
		    "指定しないと帯だけが唐突に出入りする。空なら帯なし。");
		KUJATA_REGISTER_STRING_NAMED_TIP(bossHudName_, "Boss HUD",
		    "演出中は伏せておくボスのHPバーのGameObject名。**終わってから出す**ことで、\n"
		    "「戦いが始まった」区切りになる。空なら触らない。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(allowSkip_, "Allow Skip",
		    "何かボタンを押したら飛ばせるようにするか。**繰り返し挑む相手なので基本はON。**");
	}

	KUJATA_FIELD_STRING(bossName_, "GuardianSpline");
	KUJATA_FIELD_FLOAT(bossLookHeight_, 7.0f);
	KUJATA_FIELD_FLOAT(revealSeconds_, 3.0f);
	KUJATA_FIELD_FLOAT(revealStartDistance_, 14.0f);
	KUJATA_FIELD_FLOAT(revealEndDistance_, 34.0f);
	KUJATA_FIELD_FLOAT(revealStartHeight_, 1.2f);
	KUJATA_FIELD_FLOAT(revealEndHeight_, 15.0f);
	KUJATA_FIELD_FLOAT(revealOrbitDeg_, 55.0f);
	KUJATA_FIELD_FLOAT(nameSeconds_, 2.0f);
	KUJATA_FIELD_FLOAT(nameFadeSeconds_, 0.5f);
	KUJATA_FIELD_FLOAT(approachSeconds_, 1.8f);
	KUJATA_FIELD_FLOAT(followDistance_, 10.0f);
	KUJATA_FIELD_FLOAT(followHeight_, 3.2f);
	KUJATA_FIELD_FLOAT(pivotHeight_, 1.5f);
	KUJATA_FIELD_FLOAT(blendSpeed_, 9.0f);
	KUJATA_FIELD_STRING(nameObjectName_, "BossIntroName");
	KUJATA_FIELD_STRING(nameTextName_, "BossIntroNameText");
	KUJATA_FIELD_STRING(nameBgName_, "BossIntroNameBG");
	KUJATA_FIELD_STRING(bossHudName_, "BossHPBar");
	KUJATA_FIELD_BOOL(allowSkip_, true);

	// --- 実行状態(シリアライズしない。OnPlayStartで必ず戻す) ---
	Phase phase_ = Phase::Done;
	float timer_ = 0.0f;
	// 「見上げ」を終えた位置。ここから背後の着地点へ繋ぐ。
	KujataEngine::Vector3 revealEndPosition_ = {0.0f, 0.0f, 0.0f};
	KujataEngine::Vector3 revealEndLookAt_ = {0.0f, 0.0f, 0.0f};
	// 止める前の頭脳の有効状態。**戻すときはこれをそのまま書き戻す。**
	struct SuspendedBrain {
		KujataEngine::Component* component = nullptr;
		bool wasEnabled = false;
	};
	std::vector<SuspendedBrain> suspendedBrains_;
	bool actorsSuspended_ = false;
	KujataEngine::GameObject* boss_ = nullptr;
	// 帯の元の濃さ。**掛け算の上限**にするので、演出に入る前に1回だけ控える
	// (毎フレーム掛け直すと、薄まった値がそのまま次フレームの上限になって消えてしまう)。
	float backdropBaseAlpha_ = 1.0f;
	bool backdropBaseCaptured_ = false;
};
