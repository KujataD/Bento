#pragma once

#include <KujataEngine.h>
#include <string>
#include <vector>

namespace KujataEngine {
class OrbitCameraComponent;
}
class EnemyHealth;

/// <summary>
/// **第1形態を削り切った瞬間に入る、第2形態への繋ぎの演出。** ボス戦シーンに1つ置く。
///
/// 「倒した」ではなく「天井を撃ち抜いて外へ出た」という筋書きなので、
/// 撃破の文字は出さずに、そのまま次のシーンへ送り出す。
///
/// 拍の組み立て:
///   1. 暗転             … 戦闘の音と絵をいったん切る
///   2. 中央へ跳ぶ       … 放物線で闘技場の中心へ。**画面が明けた瞬間に位置が変わっている**
///   3. 真上へビーム連射 … **毎秒20発**の掃射。撃つたびに狙いを少しずつずらす
///   3'. 揺れと落下物    … **1発ごとに**画面が揺れ、**1発ごとに**上から土埃が降ってくる。
///                         撃ち終わってからまとめて落とすと因果が切れて、ただの順番待ちに見える。
///                         天井そのものは映さない(カメラは上からボスを見下ろしている)
///   4. 巨大な土埃       … 崩れてきたものが降り注ぐ
///   5. 土埃で暗転       … そのまま暗転しきる
///   6. シーン切替       … 明けた先は空の下(BossPhase2Scene)
///
/// カメラは `OrbitCameraComponent::SetCutsceneShot` で毎フレーム預かる。
/// **見下ろしの画にしているのは天井を作らずに済ませるため**で、
/// 撃ち上げたビームは画面の上端へ抜けていき、当たった先は揺れと土埃だけで語る。
/// </summary>
class Phase2Cutscene : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "Phase2Cutscene"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	/// <summary>演出を始める(GameFlowManagerがボスのHP切れで呼ぶ)。既に始まっていれば何もしない。</summary>
	void Begin();

	/// <summary>演出中か。**この間GameFlowManagerは勝敗を判定しない。**</summary>
	bool IsPlaying() const { return phase_ != Phase::Idle; }

	/// <summary>シーン内のPhase2Cutsceneを探す(無ければnullptr)。</summary>
	static Phase2Cutscene* FindInScene(KujataEngine::Scene* scene);

private:
	enum class Phase {
		Idle,
		FadeToBlack, // 1. 暗転
		Leap,        // 2. 中央へ跳ぶ(暗転の裏で位置を作る)
		Volley,      // 3. 真上へビーム連射 + 揺れ
		Dust,        // 4. 巨大な土埃
		FadeOut,     // 5. 土埃ごと暗転
		Leaving,     // 6. シーン切替を発行済み
	};

	/// <summary>カメラを預かって毎フレーム画を作る(見下ろし+揺れ)。</summary>
	void UpdateShot(float shake);
	/// <summary>ボスを探す(名前で1回だけ解決)。</summary>
	KujataEngine::GameObject* FindBoss();
	/// <summary>ビームの器を確保する(Prefabから遅延生成)。</summary>
	KujataEngine::GameObject* AcquireBeam();
	/// <summary>ビームを真上へ立てる。lengthは長さ、thicknessは太さ。0以下で隠す。</summary>
	void ShowBeam(const KujataEngine::Vector3& origin, float length, float thickness, float tiltYaw, float tiltPitch);
	/// <summary>シーン内のOrbitCameraComponentを1つ返す。</summary>
	KujataEngine::OrbitCameraComponent* FindCamera();

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(bossName_, "Boss Name", "跳ばせるボスのGameObject名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(nextSceneName_, "Next Scene", "演出のあとに読み込むシーン名(第2形態の舞台)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(beamPrefabPath_, "Beam Prefab", "撃ち上げるビームの器のPrefab。");
		KUJATA_REGISTER_VECTOR3_NAMED_TIP(arenaCenter_, "Arena Center", 0.5f, -500.0f, 500.0f, "跳んでいく先(闘技場の中心)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(fadeInSeconds_, "Fade To Black", 0.05f, 0.0f, 5.0f, "1. 最初の暗転にかける秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(holdBlackSeconds_, "Hold Black", 0.05f, 0.0f, 5.0f, "暗転しきってから明けるまでの間。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(leapSeconds_, "Leap Seconds", 0.05f, 0.1f, 6.0f, "2. 中央へ跳ぶのにかける秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(leapHeight_, "Leap Height", 0.1f, 0.0f, 40.0f, "跳躍の高さ。");
		KUJATA_REGISTER_INT_NAMED_TIP(shotCount_, "Shot Count", 1.0f, 1, 30, "3. 撃ち上げる発数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(shotInterval_, "Shot Interval", 0.01f, 0.01f, 3.0f,
		    "1発あたりの間隔[秒]。既定の0.05秒は**毎秒20発**の掃射になる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(shotJitter_, "Shot Jitter", 0.05f, 0.0f, 10.0f,
		    "1発ごとに狙いをずらす角度[度]。**発射口は動かさず、向きだけ振る。**"
		    "根元まで動かすと、ビームが目から生えていないように見えてしまう。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(shakeStrength_, "Shake Strength", 0.05f, 0.0f, 5.0f, "3'. 着弾のたびに画面を揺らす量。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(shakeSeconds_, "Shake Seconds", 0.01f, 0.0f, 2.0f, "1発ぶんの揺れが収まるまでの秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dustSeconds_, "Dust Seconds", 0.05f, 0.0f, 8.0f, "4. 土埃を撒いている秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dustStrength_, "Dust Strength", 0.1f, 0.1f, 20.0f, "土埃の量。**ここは派手にしてよい。**");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dustFadeSeconds_, "Dust Fade", 0.05f, 0.0f, 6.0f, "5. 土埃ごと暗転しきるまでの秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(cameraHeight_, "Camera Height", 0.1f, 1.0f, 60.0f, "見下ろすカメラの高さ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(cameraBack_, "Camera Back", 0.1f, 0.0f, 60.0f, "見下ろすカメラの引き(水平距離)。");
	}

	KUJATA_FIELD_STRING(bossName_, "GuardianSpline");
	KUJATA_FIELD_STRING(nextSceneName_, "BossPhase2Scene");
	KUJATA_FIELD_STRING(beamPrefabPath_, "Prefabs/GuardianBeam.prefab.json");
	KUJATA_FIELD_VECTOR3(arenaCenter_, (KujataEngine::Vector3{0.0f, 0.0f, 0.0f}));
	KUJATA_FIELD_FLOAT(fadeInSeconds_, 0.7f);
	KUJATA_FIELD_FLOAT(holdBlackSeconds_, 0.5f);
	KUJATA_FIELD_FLOAT(leapSeconds_, 1.1f);
	KUJATA_FIELD_FLOAT(leapHeight_, 12.0f);
	KUJATA_FIELD_INT(shotCount_, 20);
	KUJATA_FIELD_FLOAT(shotInterval_, 0.05f);
	KUJATA_FIELD_FLOAT(shotJitter_, 3.0f);
	KUJATA_FIELD_FLOAT(shakeStrength_, 3.2f);
	KUJATA_FIELD_FLOAT(shakeSeconds_, 0.55f);
	KUJATA_FIELD_FLOAT(dustSeconds_, 2.2f);
	KUJATA_FIELD_FLOAT(dustStrength_, 9.0f);
	KUJATA_FIELD_FLOAT(dustFadeSeconds_, 1.2f);
	KUJATA_FIELD_FLOAT(cameraHeight_, 22.0f);
	KUJATA_FIELD_FLOAT(cameraBack_, 13.0f);

	// --- 実行状態(シリアライズしない。OnPlayStartで必ず戻す) ---
	Phase phase_ = Phase::Idle;
	float timer_ = 0.0f;
	// 何発撃ったか。
	int shotsFired_ = 0;
	// 直近の着弾からの経過[s](揺れの減衰に使う)。
	float shakeTimer_ = 0.0f;
	// 土埃を次に撒くまでの残り[s]。
	float dustTimer_ = 0.0f;
	// 跳躍の始点。
	KujataEngine::Vector3 leapStart_ = {0.0f, 0.0f, 0.0f};
	// ビームの器(Prefabから遅延生成)。
	KujataEngine::GameObject* beam_ = nullptr;
	bool beamTried_ = false;
	KujataEngine::GameObject* boss_ = nullptr;
};
