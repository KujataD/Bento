#pragma once
#include <KujataEngine.h>
#include <components/AnimatorComponent.h>
#include <components/OrbitCameraComponent.h>
#include <string>

class CharacterMotor;
class PlayerHealth;
class EnemyHealth;

/// <summary>
/// 致命の一撃(フロム風のリポスト/クリティカル)。操作キャラのGameObjectに付ける。
///
/// **「窓」は体勢崩しのスタンをそのまま使う。** 敵が `EnemyHealth::IsStaggered()` の間だけ、
/// 一定距離まで近づくとプロンプトが出て、**通常攻撃と同じボタン(R2 / K)** で発動する。
///
/// **入力はこのコンポーネントでは読まない。** プロンプトが出ている相手を `GetPromptTarget()` で
/// 公開するところまでが仕事で、押下を見て `TryExecute` を呼ぶのは `Player::Update` の役目。
/// 両方で読むと1回の押下で二重に発火するので、入力の窓口は必ず1か所に保つこと。
///
/// 気持ちよさは次の4つで作っている(順に効きが大きい):
///   1. **ヒットストップ**  当たった瞬間に `Time::SetTimeScale(0)` で全部止める
///   2. **カメラの寄り**    `OrbitCameraComponent::SetDistanceScale` で距離を詰める
///   3. **出し切りの保証**  発動中は無敵+行動ロックで、必ずモーションが完走する
///   4. **特大ダメージ**    通常の数倍。体勢崩しゲージも空にしてスタンを終わらせる
///
/// **時間を止めている間は自分だけ実時間で数える**(`GetUnscaledDeltaTime`)。
/// スケールした時間で数えると、止めた瞬間に自分のタイマーも止まって二度と復帰できない。
/// </summary>
class CriticalStrikeComponent : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "CriticalStrikeComponent"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void OnPlayStop() override;
	void Update() override;

	/// <summary>致命の実行中か(他の入力を止めたい側が見る)。</summary>
	bool IsExecuting() const { return phase_ != Phase::Idle; }

	/// <summary>いま致命を出せる相手(プロンプトを出している相手)。無ければnullptr。</summary>
	KujataEngine::GameObject* GetPromptTarget() const { return promptTarget_; }

	/// <summary>
	/// **AIから致命を発動する。** 出せたらtrue。
	///
	/// 待機中のUpdateが `IsPlayerControlled()` で早期returnするのは
	/// 「1回の押下で相方の致命まで暴発する」のを防ぐための正しいガードなので、そこは触らない。
	/// 代わりに発動経路をもう1本開ける — プロンプト表示は操作キャラだけ、発動は2経路、という形にする。
	///
	/// targetがnullptrなら自分でスタン中の相手を探す。距離・行動不能・実行中はここで弾く。
	/// </summary>
	bool TryExecute(KujataEngine::GameObject* target = nullptr);

	/// <summary>致命が届く距離(AIが寄る目標に使う)。</summary>
	float GetTriggerDistance() const { return triggerDistance_; }

private:
	enum class Phase {
		Idle,     // 待機(プロンプトを出すだけ)
		Windup,   // 振りかぶり。まだ当たらない
		Hitstop,  // 当たった瞬間。時間を止めている
		Aftermath,// 決めた直後。相手が仰け反り、カメラが回り込む。**ここが見せ場**
		Recover,  // 硬直。カメラを戻しながら終わる
	};

	/// <summary>スタン中で射程内の敵を探す。見つからなければnullptr。</summary>
	KujataEngine::GameObject* FindStaggeredTarget() const;
	/// <summary>致命を開始する。</summary>
	void Begin(KujataEngine::GameObject* target);
	/// <summary>ダメージとヒットストップ。</summary>
	void Impact();
	/// <summary>演出を元へ戻す(中断・終了・Play停止のどこからでも通す)。</summary>
	void Restore();
	/// <summary>カメラの距離倍率を設定する(見つからなければ何もしない)。</summary>
	void SetCameraDistanceScale(float scale);
	/// <summary>シーン内のOrbitCameraComponentを1つ探す。無ければnullptr。</summary>
	KujataEngine::OrbitCameraComponent* FindCamera() const;
	/// <summary>
	/// カットシーンの画を毎フレーム作る。自分と相手の中点を挟んで横から捉え、
	/// progress(0→1)に合わせてゆっくり回り込む。
	/// </summary>
	void UpdateCutsceneShot(float progress);
	/// <summary>演出全体の進み具合(0→1)。カメラの回り込み量に使う。</summary>
	float CutsceneProgress() const;
	/// <summary>いま操作されているキャラか(PartyManagerがPlayerの有効/無効で示す)。</summary>
	bool IsPlayerControlled() const;

	/// <summary>
	/// プロンプト(敵の頭上のバナー)を対象の上へ置き、カメラの方を向かせる。
	/// 対象がいなければ隠す。**届いているかどうかで色を変える**のが要点で、
	/// 「今なら入る」が一目で分かるようにしている。
	/// </summary>
	void UpdatePrompt(KujataEngine::GameObject* target);
	/// <summary>プロンプトの器(Prefabから遅延生成)。</summary>
	KujataEngine::GameObject* AcquirePrompt();

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(criticalClipName_, "Critical Clip",
		    "致命の一撃で再生するクリップ名。空なら現在の見た目のまま(ヒットストップとダメージだけ)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(triggerDistance_, "Trigger Distance", 0.05f, 0.5f, 20.0f,
		    "スタンした敵にこの距離まで近づくとプロンプトが出る。大きいほど繋げやすい。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(windupSeconds_, "Windup Seconds", 0.01f, 0.0f, 3.0f,
		    "発動してから当たるまでの時間[秒]。振りかぶりの長さ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(recoverSeconds_, "Recover Seconds", 0.01f, 0.0f, 3.0f,
		    "当たった後の硬直[秒]。ここが短すぎると余韻が無く、長すぎるともたつく。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(hitstopSeconds_, "Hitstop Seconds", 0.005f, 0.0f, 1.0f,
		    "当たった瞬間に時間を止める長さ[秒]。**これが打撃の重さを決める最大の要素**。0.08〜0.15が目安。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(hitstopTimeScale_, "Hitstop Time Scale", 0.01f, 0.0f, 1.0f,
		    "ヒットストップ中の時間の速さ。0で完全停止、0.1くらいにすると「ぬるっと」した重さになる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(damage_, "Damage", 1.0f, 0.0f, 100000.0f,
		    "致命のダメージ。通常攻撃の数倍にすると「決めた」感触になる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(cameraDistanceScale_, "Camera Distance Scale", 0.01f, 0.1f, 1.0f,
		    "致命中のカメラ距離の倍率。0.6くらいで寄る。1で寄らない。");
		KUJATA_REGISTER_STRING_NAMED_TIP(promptPrefabPath_, "Prompt Prefab",
		    "敵の頭上に出すプロンプトのPrefab(World Canvas)。空なら表示なしで機能だけ動く。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(promptHeight_, "Prompt Height", 0.05f, 0.0f, 20.0f,
		    "プロンプトを敵の狙い点からどれだけ上に出すか。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(promptShowDistance_, "Prompt Show Distance", 0.05f, 0.5f, 60.0f,
		    "この距離まで近づくとバナーが出る。**Trigger Distanceより広く**取り、\n"
		    "遠い間は暗く、届いたら光る、という二段階で「あと少し」を伝える。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(aftermathSeconds_, "Aftermath Seconds", 0.05f, 0.0f, 6.0f,
		    "決めた後の見せ場の長さ[秒]。**演出全体の大半をここが占める**。\n"
		    "この間、相手は仰け反り、カメラは回り込み、こちらは無敵のまま動けない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(shotSideOffset_, "Shot Side Offset", 0.05f, 0.0f, 20.0f,
		    "カットシーンのカメラを横へどれだけ振るか。2人を横から挟んで捉えるための距離。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(shotHeight_, "Shot Height", 0.05f, -5.0f, 20.0f, "カメラの高さ(中点からの差)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(shotBackOffset_, "Shot Back Offset", 0.05f, -20.0f, 20.0f,
		    "自分側へどれだけ引くか。大きいほど引きの画になる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(shotOrbitDeg_, "Shot Orbit", 1.0f, -180.0f, 180.0f,
		    "カットシーン中にカメラが回り込む角度[度]。**止まった絵にしないための要**。0で固定カメラ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(strikeOffset_, "Strike Offset", 0.05f, 0.0f, 10.0f,
		    "発動時に相手からこの距離へ吸い付く。離れた位置から始まると空振りに見えるため。");
	}

	// 致命のクリップ名。
	KUJATA_FIELD_STRING(criticalClipName_, "PawnAttackCharge");
	// プロンプトが出る距離。
	KUJATA_FIELD_FLOAT(triggerDistance_, 3.0f);
	// 振りかぶり / 硬直 / ヒットストップ。
	KUJATA_FIELD_FLOAT(windupSeconds_, 0.35f);
	KUJATA_FIELD_FLOAT(recoverSeconds_, 0.55f);
	KUJATA_FIELD_FLOAT(hitstopSeconds_, 0.12f);
	KUJATA_FIELD_FLOAT(hitstopTimeScale_, 0.0f);
	// ダメージ。
	KUJATA_FIELD_FLOAT(damage_, 120.0f);
	// カメラの寄り。
	KUJATA_FIELD_FLOAT(cameraDistanceScale_, 0.6f);
	// プロンプト。
	KUJATA_FIELD_STRING(promptPrefabPath_, "Prefabs/CriticalPrompt.prefab.json");
	KUJATA_FIELD_FLOAT(promptHeight_, 1.2f);
	KUJATA_FIELD_FLOAT(promptShowDistance_, 12.0f);
	// 吸い付く距離。
	KUJATA_FIELD_FLOAT(strikeOffset_, 1.8f);

	// 決めた後の見せ場の長さ。ここが演出の大半を占める。
	KUJATA_FIELD_FLOAT(aftermathSeconds_, 1.9f);
	// カットシーンのカメラ配置。
	KUJATA_FIELD_FLOAT(shotSideOffset_, 3.2f);
	KUJATA_FIELD_FLOAT(shotHeight_, 1.9f);
	KUJATA_FIELD_FLOAT(shotBackOffset_, 1.4f);
	KUJATA_FIELD_FLOAT(shotOrbitDeg_, 38.0f);
	// 通常視点からカットシーンへ寄せる速さ[1/s]。小さいほどゆっくり切り替わる。
	KUJATA_FIELD_FLOAT(cameraBlendSpeed_, 6.0f);

	// --- 実行状態 ---
	Phase phase_ = Phase::Idle;
	// 現フェーズの残り時間[s]。**常に実時間で数える**(止めている間も進むように)。
	float phaseTimer_ = 0.0f;
	// 演出開始からの経過[s]。カメラの回り込みに使うので実時間で積む。
	float cutsceneElapsed_ = 0.0f;
	// 実行中の相手。
	KujataEngine::GameObject* target_ = nullptr;
	// いまプロンプトを出している相手(実行中でなくても入る)。
	KujataEngine::GameObject* promptTarget_ = nullptr;

	// プロンプトの器(Playごとに生成)と、その背景Image(色を変える先)。
	KujataEngine::GameObject* prompt_ = nullptr;
	bool promptTried_ = false;

	// 同じGameObjectの技(致命の決め方を技側に任せるため)。
	class IAbilitySet* abilitySet_ = nullptr;

	CharacterMotor* motor_ = nullptr;
	PlayerHealth* health_ = nullptr;
	KujataEngine::AnimatorComponent* animator_ = nullptr;
};
