#pragma once
#include <KujataEngine.h>
#include <components/AnimatorComponent.h>

/// <summary>
/// プレイアブルキャラクター共通の「体」。移動・旋回・回避(前転)・ノックバック・無敵・
/// ルートモーション転送を担当する。入力読み取りもAIも持たない。
///
/// 操作は「頭脳」側のComponentがこのAPIを呼んで行う:
///   Player(手動入力) / AllyAIBrain(BT) のどちらからでも同じように動かせる。
/// PawnとBishopの性能差はインスペクタの数値とクリップ名だけで、コードは共有する。
///
/// 状態(スタン・回避)による行動制限は本Componentが内部で守る。
/// MoveCameraRelative/MoveWorldは行動不能中は無視され、TryDodgeは不可時にfalseを返すため、
/// 頭脳側は毎フレーム呼ぶだけでよい。
/// </summary>
class CharacterMotor : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "CharacterMotor"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	// --- 頭脳(入力/AI)から呼ぶ操作API ---

	/// <summary>
	/// カメラ基準の入力ベクトル(x=横, z=前後)で移動し、移動方向へ旋回する(手動操作用)。
	/// 行動不能中(スタン・回避中)は何もしない。
	/// </summary>
	void MoveCameraRelative(const KujataEngine::Vector3& input);

	/// <summary>
	/// ワールド空間の方向へ移動し、その方向へ旋回する(AI用。カメラに依存しない)。
	/// speedScaleで基本速度に対する倍率を指定できる(追従の歩き/走り分け等)。
	/// 行動不能中(スタン・回避中)は何もしない。
	/// </summary>
	void MoveWorld(const KujataEngine::Vector3& direction, float speedScale = 1.0f);

	/// <summary>
	/// ワールド空間の方向へ旋回だけ行う(移動しない。AIの狙い定め用)。
	/// 行動不能中(スタン・回避中)は何もしない。
	/// </summary>
	void FaceWorld(const KujataEngine::Vector3& direction);

	/// <summary>
	/// 回避(前転)を開始する(無敵付与+前転クリップ再生)。向いている方向へ転がる。
	/// 行動不能中・クールダウン中・スタミナ切れでは開始せずfalseを返す。
	/// スタミナは Dodge Stamina Cost[%] だけ消費する(攻撃・ガードと同じく残量>0なら出せて0で止まる)。
	/// </summary>
	bool TryDodge();

	/// <summary>
	/// カメラ基準の入力方向へ回避する(Z注目中用。入力方向へ即座に向き直ってから転がる)。
	/// 入力がほぼゼロなら向いている方向へ(TryDodgeと同じ)。
	/// </summary>
	bool TryDodgeCameraRelative(const KujataEngine::Vector3& input);

	/// <summary>
	/// ワールド空間の方向へ回避する(AI用。カメラに依存しない)。
	/// **向きを変える前に可否判定を通す** — でないとスタミナ切れのとき「その場で向きだけ変わる」ことになる。
	/// </summary>
	bool TryDodgeWorld(const KujataEngine::Vector3& direction);

	/// <summary>いま回避を開始できるか(行動不能・クールダウン・スタミナ切れでない)。AIの先読み用。</summary>
	bool CanDodge() const;

	/// <summary>
	/// 注目対象を設定する(Z注目)。設定中はMoveCameraRelativeが「入力方向へ移動しつつ対象の方を向く」
	/// ストレイフ移動になる。nullptrで解除(通常の移動方向への旋回に戻る)。
	/// </summary>
	void SetFacingTarget(KujataEngine::GameObject* target) { facingTarget_ = target; }
	KujataEngine::GameObject* GetFacingTarget() const { return facingTarget_; }

	/// <summary>移動速度の倍率(ガード中の鈍足など)。毎フレーム頭脳側が設定する。1で等速。</summary>
	void SetMoveSpeedScale(float scale) { moveSpeedScale_ = scale; }

	/// <summary>
	/// 旋回速度の倍率(攻撃モーション中の方向補正など)。毎フレーム頭脳側が設定する。1で等速。
	/// **MoveCameraRelativeにだけ効く。** AIが使うMoveWorldは素のTurn Speedのまま。
	/// </summary>
	void SetTurnSpeedScale(float scale) { turnSpeedScale_ = scale; }

	/// <summary>
	/// 進行中の移動をすべて止める(ノックバック・回避・Rigidbodyの速度)。
	/// **死亡した瞬間のように「その場で完全に止めたい」ときに使う。**
	/// 個別に消すと、回避の途中で死んだ場合だけ滑り続ける、といった取りこぼしが出る。
	/// </summary>
	void StopAllMotion();

	/// <summary>
	/// 一定時間その場に固定する(致命の一撃など、専用モーションを出し切らせたいとき)。
	/// 硬直と同じ扱いなので移動・攻撃・回避を受け付けなくなるが、
	/// **ノックバックものけぞりクリップも起こさない**点が被弾と違う。
	/// </summary>
	void BeginActionLock(float seconds);

	/// <summary>
	/// ノックバックを与える。stunDurationの間は行動不能になり、初速velocityから減衰しながら滑る。
	/// 回避中(無敵切れ後)に受けた場合は回避を中断してクールダウンへ移行する。
	/// </summary>
	void ApplyKnockback(const KujataEngine::Vector3& velocity, float stunDuration);

	// --- 状態クエリ ---

	/// <summary>硬直中(被弾でしばらく動けない状態)か。</summary>
	bool IsStunned() const { return stunTimer_ > 0.0f; }

	/// <summary>回避(前転)中か。</summary>
	bool IsDodging() const { return dodgeTimer_ > 0.0f; }

	/// <summary>回避後のクールダウン中か(再回避のみ不可。移動や攻撃は可能)。</summary>
	bool IsDodgeCooldown() const { return dodgeCooldownTimer_ > 0.0f; }

	/// <summary>行動不能(硬直・回避中)か。攻撃入力などの抑制に使う。クールダウンは含まない。</summary>
	bool IsActionLocked() const { return IsStunned() || IsDodging(); }

	/// <summary>基本移動速度[unit/s]。AIが「この時間で何m逃げられるか」を計算するのに使う。</summary>
	float GetSpeed() const { return speed_; }

	/// <summary>回避の変位[m]の目安(速度×尺)。AIが回避で足りるかを判断するのに使う。</summary>
	float GetDodgeDistance() const;

private:
	void UpdateKnockback(float deltaTime);
	/// <summary>回避中の前進と終了判定。終了時にクールダウンへ移行する。</summary>
	void UpdateDodge(float deltaTime);
	/// <summary>無敵時間の残りを進め、切れたら無敵を解除する。</summary>
	void TickInvincibility(float deltaTime);
	/// <summary>
	/// モデル(子)に溜まったルートモーションの水平移動分を最上位(コライダー側)へ移す。
	/// 攻撃の踏み込み(moveForward)で実際の当たり判定も前進させるため。
	/// 上下(y)はボビング等の見た目専用なので移さない。
	/// </summary>
	void TransferModelRootMotion();

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT(speed_, 0.05f, 0.0f, 100.0f);
		KUJATA_REGISTER_FLOAT_NAMED(turnSpeed_, "Turn Speed", 0.1f, 0.0f, 50.0f);
		KUJATA_REGISTER_FLOAT_NAMED(knockbackDamping_, "Knockback Damping", 0.1f, 0.0f, 50.0f);
		KUJATA_REGISTER_INT_NAMED(dodgeDurationFrames_, "Dodge Duration Frames", 1.0f, 1, 600);
		KUJATA_REGISTER_INT_NAMED(invincibleFrames_, "Dodge Invincible Frames", 1.0f, 0, 600);
		KUJATA_REGISTER_INT_NAMED(dodgeCooldownFrames_, "Dodge Cooldown Frames", 1.0f, 0, 600);
		KUJATA_REGISTER_FLOAT_NAMED(dodgeSpeed_, "Dodge Speed", 0.1f, 0.0f, 100.0f);
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dodgeStaminaCost_, "Dodge Stamina Cost", 1.0f, 0.0f, 100.0f,
		    "回避1回のスタミナ消費[最大値比%]。\n"
		    "残量が0より大きければ回避でき、消費で0に張り付く(攻撃・ガードと同じ流儀)。\n"
		    "StaminaComponentが付いていないキャラは消費しない。");
		KUJATA_REGISTER_STRING_NAMED(dodgeClipName_, "Dodge Clip");
		KUJATA_REGISTER_STRING_NAMED(recoilClipName_, "Recoil Clip");
	}

	// 移動速度[unit/s]
	KUJATA_FIELD_FLOAT(speed_, 5.0f);
	// 旋回速度[rad/s]
	KUJATA_FIELD_FLOAT(turnSpeed_, 12.0f);
	// ノックバック速度の減衰の強さ(大きいほどすぐ止まる)
	KUJATA_FIELD_FLOAT(knockbackDamping_, 6.0f);

	// --- 回避 ---
	// 回避(前転)の長さ[フレーム, 60fps基準]。回避クリップの尺に合わせるとよい。
	KUJATA_FIELD_INT(dodgeDurationFrames_, 30);
	// 回避開始からの無敵時間[フレーム]。
	KUJATA_FIELD_INT(invincibleFrames_, 27);
	// 回避終了後、再び回避できるまでのクールダウン[フレーム](移動や攻撃は可能)。
	KUJATA_FIELD_INT(dodgeCooldownFrames_, 10);
	// 回避中の前進速度[unit/s]。
	KUJATA_FIELD_FLOAT(dodgeSpeed_, 8.0f);
	// 回避1回のスタミナ消費[最大値比%]。
	KUJATA_FIELD_FLOAT(dodgeStaminaCost_, 20.0f);

	// --- クリップ名(キャラごとに差し替え可能。空なら再生しない) ---
	// 回避(前転)クリップ。
	KUJATA_FIELD_STRING(dodgeClipName_, "PlayerRoll");
	// 被弾のけぞりクリップ。
	KUJATA_FIELD_STRING(recoilClipName_, "PlayerRecoil");

	// モデル側(子)のAnimator。コリジョンはこの最上位、見た目とアニメーションは子が担当する。
	KujataEngine::AnimatorComponent* animator_ = nullptr;
	// Animatorを持つモデルのGameObject(ルートモーション転送先の参照元)。
	KujataEngine::GameObject* modelObject_ = nullptr;

	// --- 被弾状態 ---
	// 残り硬直時間[s]。0より大きい間は移動指示を受け付けない。
	float stunTimer_ = 0.0f;
	// ノックバックの現在速度。指数減衰させながらtranslationへ加算する。
	KujataEngine::Vector3 knockbackVelocity_ = {0.0f, 0.0f, 0.0f};

	// --- 回避状態 ---
	// 回避の残り時間[s]。0より大きい間は前転しながら前進し、他の行動はできない。
	float dodgeTimer_ = 0.0f;
	// 回避クールダウンの残り時間[s]。0より大きい間は再回避できない(移動や攻撃は可能)。
	float dodgeCooldownTimer_ = 0.0f;
	// 無敵の残り時間[s]。切れたらPlayerHealthの無敵を解除する。
	float invincibleTimer_ = 0.0f;

	// 同じGameObjectのPlayerHealth(無敵の付与先)。
	class PlayerHealth* health_ = nullptr;
	// 同じGameObjectのStaminaComponent(無ければ回避はスタミナを消費しない)。
	class StaminaComponent* stamina_ = nullptr;

	// --- 注目 ---
	// 注目対象(nullptr=非注目)。移動中もこちらを向き続ける。
	KujataEngine::GameObject* facingTarget_ = nullptr;
	// 移動速度倍率(ガード中など)。
	float moveSpeedScale_ = 1.0f;
	// 旋回速度倍率(攻撃中の方向補正など)。
	float turnSpeedScale_ = 1.0f;
};
