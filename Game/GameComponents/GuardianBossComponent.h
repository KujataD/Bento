#pragma once

#include <BahamutAI/AI.h>
#include <KujataEngine.h>
#include <memory>
#include <string>
#include <vector>

class IGuardianLegRig;
class GuardianBody;

/// <summary>
/// ガーディアンの「頭脳」。BehaviorTree(BahamutAI)で行動を組み立てる。
/// ツリー本体はBahamutAIEditorで編集し、ここではCondition/Actionの実装と登録だけを行う。
/// 調整値はすべてBTノードのParamsで指定する(HammerEnemyComponentと同じ流儀)。
///
/// ノードは「動作単位」で切ってあり、ツリー側で組み替えて行動を作る。
///
/// 登録Condition:
///   IsTargetWithin  : 対象との水平距離が distance 以内か
///
/// 登録Action(移動):
///   MoveToTarget    : speed分だけ対象へ向かって進む。**speedを負にすると後退する**
///   FaceTarget      : 対象の方向へ旋回する
///   BodySpin        : 胴体(球体)だけを回す。脚は接地したまま
///
/// 登録Action(足の攻撃。Sequenceで 振り上げ→踏み下ろし→復帰 と繋いで1つの攻撃にする):
///   StompRaise      : 脚を1本選んで振り上げる(曲線レイヤーへ主導権を移す)
///   StompSlam       : 対象へ向けて踏み下ろす。攻撃判定ON
///   StompRecover    : 脚を歩行へ返す(曲線レイヤーの主導権を戻す)
///   LegSweep        : 振り上げた脚を横へ薙ぎ払う。攻撃判定ON
///
/// 登録Action(飛びかかり。遠距離から一気に詰めて全脚で踏み潰す):
///   LeapCharge      : 溜め。沈み込みつつ対象を向き、着地点を決める
///   LeapFly         : 放物線で着地点まで飛ぶ。4本とも脚を畳む
///   LeapSlam        : 着地。4本すべてを外へ叩きつける。全脚の攻撃判定ON
///   LeapRecover     : 全脚を歩行へ返す
///
/// 登録Action(コマ回転。胴体を地面に接地させ、脚を突っ張らせたまま body ごと高速回転する):
///   JetWarn         : **予兆**。沈み込んで脚を抱え込み、胴体を赤く点滅させながらゆっくり回り始める。判定は出さない
///   JetLiftOff      : コマの構えへ移る。脚を外へ突っ張らせ、胴体ごと回し始める
///   JetChase        : コマのまま対象へ寄っていく。**回転中は全脚の攻撃判定ON**
///   JetLand         : 回転を落として着地し、脚を歩行へ返す
///
/// 登録Action(ビーム。脚を使わない遠距離攻撃。子オブジェクト "Beam" を伸縮させるだけ):
///   BeamCharge      : 予告。点滅しながら正面を取る。判定は出さない
///   BeamFire        : 照射。攻撃判定ON。旋回を遅くすると薙ぎ払いになり避けられる
///   BeamRecover     : 細めて消す
///
/// 攻撃は GuardianSplineRig の曲線レイヤー(leg{i}.curveWeight / curveTarget)を直接動かす。
/// 歩行側(GuardianGait)は curveWeight が0.5を超えている間その脚の踏み出しを止め、
/// 戻った瞬間に踏み直すので、攻撃と歩行の受け渡しは自動で噛み合う。
/// </summary>
class GuardianBossComponent : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "GuardianBossComponent"; }
	bool AllowMultiple() const override { return false; }

	void Initialize() override;
	void OnPlayStart() override;
	void Update() override;

	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;

private:
	void LoadBTSet();

	// --- BT Conditions ---
	bool IsTargetWithin(const BahamutAI::NodeParams& params);

	// --- BT Actions ---
	BahamutAI::BTStatus MoveToTarget(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus FaceTarget(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus BodySpin(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus StompRaise(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus StompSlam(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus StompRecover(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus LegSweep(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus LeapCharge(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus LeapFly(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus LeapSlam(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus LeapRecover(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus JetWarn(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus JetLiftOff(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus JetChase(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus JetLand(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	// --- ビーム(遠距離)。脚を一切使わないので、歩行と完全に独立して撃てる ---
	BahamutAI::BTStatus BeamCharge(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus BeamFire(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus BeamRecover(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	// --- 目(Eye)の攻撃。**脚をまったく使わない。**
	//     目は土台から完全に分離しているので、これらは歩行・接合部・脚の攻撃と一切干渉しない。
	BahamutAI::BTStatus EyeAimUp(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus EyeBeamSlam(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus EyeBeamSpin(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus EyeDetach(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus EyeMerge(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	// --- 第2形態(空中戦)。目は浮いたまま、脚は地を這う。
	//
	// **地面に予告の円や線は一切描かない。** 読ませるのはボスの動きだけで、
	// 各アクションは「溜め(はっきり見える予備動作)」→「本番」の2フェーズに割ってある。
	// 溜めの姿勢がそのまま「次に何が来るか」の情報になるように、攻撃ごとに別の形を取らせる。

	/// <summary>目の周りに光球を生み(見える溜め)、順に落とす。着弾ごとに小さな衝撃波。</summary>
	BahamutAI::BTStatus EyeStarfall(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	/// <summary>目が膨らんでから胞子弾をばら撒く。着弾点に**居座る**危険域を残す。</summary>
	BahamutAI::BTStatus EyeSporeBurst(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	/// <summary>脚を後ろへ大きく引き絞ってから、地を這って直線に突進する。</summary>
	BahamutAI::BTStatus LegCharge(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	/// <summary>目のビーム薙ぎと脚の突進を**同時に**出すコンボ。線が2本走る。</summary>
	BahamutAI::BTStatus CrossfireCombo(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	/// <summary>目を指定の高さへ浮かべたまま保つ(第2形態の常態)。対象へゆっくり漂う。</summary>
	BahamutAI::BTStatus EyeHover(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	/// <summary>漂いながら撃つ小さな弾(EyeHoverから毎Tick呼ぶ)。脚が攻撃中でも目が黙らないようにする。</summary>
	void EyeSnipe(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params, GuardianBody* body,
	    KujataEngine::GameObject* target);
	/// <summary>脚だけで対象へ這い寄る(目は浮いたまま)。</summary>
	BahamutAI::BTStatus LegsCrawl(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	/// <summary>
	/// **脚を少し広げ、接地したまま回りながら追いかける。**
	/// 薙ぎ払い(LegSweep)は脚が地面を滑るだけで当たらなかったので、その置き換え。
	/// 回っている脚そのものが刃になり、逃げる相手を押し続ける。
	/// </summary>
	BahamutAI::BTStatus LegSpinChase(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	/// <summary>
	/// **追尾弾をばら撒く(術師の弾と同じ挙動)。** 置き型の胞子(EyeSporeBurst)の置き換え。
	/// 撃ったあとも曲がってくるので、立ち位置をずらすだけでは避けられない。
	/// </summary>
	BahamutAI::BTStatus EyeHomingVolley(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	/// <summary>
	/// **短いビームを狙って連射する。** 振り下ろし(EyeBeamSlam)や全周薙ぎ(EyeBeamSpin)とは別種で、
	/// 1発ごとに撃つ直前の相手を狙い直すので、動き続けていないと当たる。
	/// </summary>
	BahamutAI::BTStatus EyeLaserBurst(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	// --- 弾(光球/胞子)。EnemyWeaponを持つPrefabをプールして、位置だけコードで動かす ---

	/// <summary>弾を1つ確保して撃ち出す。fallは真下へ落ちる弾、lingerは着弾後その場に残る弾。</summary>
	void SpawnOrb(const KujataEngine::Vector3& position, const KujataEngine::Vector3& velocity, float radius, float lifetime,
	    bool linger, float damage, KujataEngine::GameObject* homingTarget = nullptr, float homingTurnRate = 0.0f);
	/// <summary>弾を進める(毎フレーム)。地面に着いたら消えるか、残留域になる。</summary>
	void UpdateOrbs(float deltaTime);
	/// <summary>出ている弾を全部畳む(中断経路から必ず通すこと)。</summary>
	void ClearOrbs();

	/// <summary>
	/// 脚を定位置の向きのまま、接合部からまっすぐ外へ突っ張らせた位置を全脚ぶん書き込みます。
	/// **脚自体は回しません。** 回すのはルートのYaw(SpinRootYaw)で、脚も胴体も一体で回るからコマになる。
	/// 脚ごとに角度をずらして回すと、接合部の位置と脚の向きが噛み合わず形がいびつになる。
	/// reachRatio=1.0 かつ straight=true で「ピンと伸ばした棒」、pitchDeg=0 で地面と平行。
	/// </summary>
	void ApplyLegBlades(float reachRatio, float pitchDeg, bool straight);

	/// <summary>
	/// ルート自身のYawを回します。脚の目標はルート基準で置いてあるので、
	/// ルートを回せば脚と胴体が一体で回る。**コマの回転はここで作る。**
	/// </summary>
	void SpinRootYaw(float spinSpeedDeg, float deltaTime);

	/// <summary>
	/// 自分と全ての子孫を Body Part Layer へ移します(OnPlayStartで1回)。
	/// **目的はカメラ**: OrbitCameraComponentは遮蔽物を探すときに Obstacle Mask でレイヤーを絞るので、
	/// ボスの体を専用レイヤーへ隔離し、カメラ側のマスクからそのビットを外せば
	/// 「胴体の下に潜り込んだとき、振り回される脚にカメラが反応してガクガクする」のを断てる。
	/// 当たり判定そのものは残るので、脚で殴られるし脚を斬ることもできる。
	///
	/// Prefabのフォーマットはlayerを保存しないため、**データではなくコードで毎回入れ直す**。
	/// </summary>
	void ApplyBodyPartLayer();

	/// <summary>足元の地面の高さ。GuardianGaitが無ければ現在のルートYを返します。</summary>
	float SampleGroundUnderRoot() const;

	// --- helpers ---
	IGuardianLegRig* GetRig();

	/// <summary>
	/// 衝撃刃(広い当たり判定の器)を出します。脚の先端の球だけでは「振り回した軌跡」を拾えないため、
	/// 踏みつけ・薙ぎ払い・着地・コマ回転はこの面の判定で当てる。
	/// Prefabからランタイム生成して使い回す(1体につき1つ)。
	///
	///   center      : 中心のワールド座標
	///   startRadius : 開始半径 / endRadius : 終了半径(広がる衝撃波なら start<end、一定なら同じ値)
	///   duration    : 出ている時間[s]。0以下なら明示的にStopShockwaveするまで出しっぱなし
	///   followOwner : trueなら毎フレーム中心をボスへ追従させる(コマ回転のように動きながら当てる場合)
	/// </summary>
	void StartShockwave(const KujataEngine::Vector3& center, float startRadius, float endRadius, float duration, bool followOwner,
	    float damage, float knockback, float stunDuration, float hitInterval);

	/// <summary>衝撃刃を消します。中断経路からも必ず通すこと。</summary>
	void StopShockwave();

	/// <summary>衝撃刃の広がりと寿命を進めます(毎フレーム呼ぶ)。</summary>
	void UpdateShockwave(float deltaTime);

	/// <summary>衝撃刃の器(Prefabから遅延生成)。失敗したらnullptr。</summary>
	KujataEngine::GameObject* AcquireShockwave();

	/// <summary>
	/// 衝撃刃の濃さ(0=透明, 1=Materialの既定値)。**Materialアセットではなく個体へ上書きする** —
	/// アセットを直接書き換えると、同時に出ている他の衝撃刃まで一緒に薄くなる。
	/// </summary>
	void ApplyShockwaveFade(float alphaScale);

	/// <summary>全脚の攻撃判定をまとめてON/OFFします(着地の踏み潰し用)。</summary>
	void SetAllStrikesActive(bool active);

	/// <summary>
	/// 脚の攻撃判定の大きさを倍率で変えます(SphereColliderの半径はTransformのスケールに追従する)。
	/// 高速で振り回す攻撃は判定がすり抜けやすいので、その間だけ太らせて確実に当てるために使う。
	/// **1.0へ戻す責任は呼び出し側にある**(AbortAttackでも必ず戻すこと)。
	/// </summary>
	void SetAllStrikeScales(float scale);

	/// <summary>
	/// 胴体を発光させます(予兆の点滅用)。activeがfalseなら元のマテリアルへ戻します。
	/// 一時的にONにするものなので、中断経路でも必ずOFFへ戻すこと。
	/// </summary>
	void SetBodyEmissive(bool active, const KujataEngine::Vector3& color, float intensity);

	/// <summary>HPを見て第2形態への移行を判定する(毎フレーム呼ぶ)。</summary>
	void UpdatePhaseTransition();
	/// <summary>第2形態へ移る(1回だけ)。見た目・威力・速さがまとめて変わる。</summary>
	void EnterPhase2();
	/// <summary>
	/// 攻撃予兆の点滅が終わったときに戻す「基準の発光」。
	/// **単にClearしてはいけない。** 第2形態では常時光っているので、消すと形態の手掛かりが消える。
	/// </summary>
	void RestoreBaseEmissive();

public:
	/// <summary>第2形態に入っているか。</summary>
	bool IsPhase2() const { return phase2_; }

	/// <summary>
	/// 行動を止める(カットシーンが体を預かっている間)。
	/// **止めないとBTが裏で動き続け**、演出で置いた位置から歩き出したり攻撃を出したりする。
	/// 出しっぱなしの判定は止めた時点で畳む。
	/// </summary>
	void SetSuspended(bool suspended);
	bool IsSuspended() const { return suspended_; }

private:

	/// <summary>胴体の高さオフセットを設定します(GuardianBodyが無ければ何もしない)。</summary>
	void SetBodySink(float offset);
	/// <summary>胴体へ追加のピッチ[rad]を与える(致命の仰け反り)。0で通常。</summary>
	void SetBodyPitchOffset(float radian);

	/// <summary>
	/// ビームの器を用意します。子オブジェクト(既定 "Beam")があればそれを、
	/// 無ければ Beam Prefab からランタイム生成して使い回します。
	///
	/// **生成した器は親を持たない(ワールド空間)。** 目のYawとピッチでビームを振り回すので、
	/// ルートの向きに引きずられない方が扱いやすく、目の回転だけで薙ぎ払いが書ける。
	/// </summary>
	KujataEngine::GameObject* AcquireBeam();

	/// <summary>ビームの器(既定では "Beam" という子オブジェクト)を探します。生成はしません。</summary>
	KujataEngine::GameObject* GetBeamObject();

	/// <summary>
	/// 目から出るビームの向き・長さ・太さをワールド空間で更新します。
	/// yawはワールドの水平角[rad]、pitchは下向き正[rad]。原点は目の位置。
	/// </summary>
	void UpdateEyeBeam(float yaw, float pitch, float length, float thickness);

	/// <summary>目のワールド位置(目が見つからなければルート+Beam Height)。</summary>
	KujataEngine::Vector3 GetEyeWorldPosition() const;

	/// <summary>ルートのワールドYaw[rad]。</summary>
	float GetRootYaw() const;

	/// <summary>対象へのワールドYaw[rad]。対象がいなければルートのYaw。</summary>
	float YawToTarget() const;
	/// <summary>目の居場所から対象を見たYaw。分離中の目でビームを狙うときはこちらを使う。</summary>
	float YawFromEyeToTarget() const;

	/// <summary>
	/// 今フレームの発光の強さを要求します。**攻撃中だけ光る**ようにするための入口で、
	/// 要求が無ければ待機の明るさへフェードで戻る。色は形態で決まる(第2形態は赤)。
	/// </summary>
	void RequestEmissive(float intensity);

	/// <summary>発光を要求値/待機値へフェードさせます(毎フレーム呼ぶ)。</summary>
	void UpdateEmissive(float deltaTime);

	/// <summary>
	/// ビームの向き・長さ・太さをまとめて更新します。ピッチだけをここで書き、ヨーはルートの旋回が担当する。
	/// Cubeは原点が中心なので、長さLにするには scale.z=L と「傾けた向きへ半分だけ前に出す」がセットで要る。
	/// BoxColliderのsizeが(1,1,1)なら当たり判定はscaleに自動追従するので、見た目と当たりがここだけで揃う。
	/// </summary>
	void UpdateBeam(float length, float thickness, float aimHeight);

	/// <summary>ビームのダメージ判定をON/OFFします。</summary>
	void SetBeamAttack(bool active);

	/// <summary>ビームを消します(判定OFF+非アクティブ化)。中断経路からも必ず通すこと。</summary>
	void HideBeam();
	/// <summary>ビームの器を出しているフェーズ名か。ここに載っていないフレームは器を畳む。</summary>
	static bool IsBeamPhase(const std::string& phase);

	/// <summary>脚を畳んだ位置(ルートローカル)。定位置の向きを保ったまま内側へ引き寄せて持ち上げる。</summary>
	KujataEngine::Vector3 TuckedLocal(int legIndex, float pull, float lift) const;

	/// <summary>脚を張り出した位置(ルートローカル)。定位置の向きのまま外側の指定の高さへ。</summary>
	KujataEngine::Vector3 SpreadLocal(int legIndex, float distance, float height) const;
	KujataEngine::GameObject* FindTarget();

	/// <summary>対象へYawだけ旋回する。ほぼ向いていればtrue。</summary>
	bool RotateTowardsTarget(float turnSpeed, float deltaTime);

	/// <summary>ワールド座標をGuardianルート基準のローカル座標へ変換します。</summary>
	KujataEngine::Vector3 ToRootLocal(const KujataEngine::Vector3& worldPosition) const;

	/// <summary>
	/// 対象へ最も向いている脚の番号を返します。paramのlegが0以上ならそれをそのまま使う。
	/// 定位置(ルートローカル)の向きと、ルートローカルでの対象方向との内積で選ぶ。
	/// </summary>
	int PickStompLeg(int requestedLeg);

	/// <summary>踏みつけに使う脚の攻撃判定(Leg*_Strike のEnemyWeapon)をON/OFFします。</summary>
	void SetStrikeActive(int legIndex, bool active);

	/// <summary>攻撃を中断して脚を歩行へ返します。</summary>
	void AbortAttack();

	// --- 体勢崩し(スタン)・のけぞり。EnemyHealthのコールバックから呼ばれる ---

	/// <summary>スタン開始。攻撃を中断しBTの分岐を捨て、スタン姿勢(UpdateStunPose)へ移る。</summary>
	void OnStaggered();
	/// <summary>スタン終了。脚を歩行へ返す。</summary>
	void OnStaggerEnd();
	/// <summary>短いのけぞり(ジャストガード時)。攻撃を中断し、Flinch Duration秒だけ胴体を沈めて行動を止める。</summary>
	void OnFlinch();
	/// <summary>
	/// ダメージを受けたときのリアクションを差し込む。**行動は中断しない。**
	/// attacker から見た向きへ傾いて沈むだけの見た目の層で、強さは strength 倍。
	/// </summary>
	void OnHitReaction(KujataEngine::GameObject* attacker, float strength);
	/// <summary>のけぞりを毎フレーム減衰させ、加算レイヤーとして書き込む。</summary>
	void UpdateHitReaction(float deltaTime);

	/// <summary>
	/// 致命を受けた。**攻撃を畳んで大きく仰け反る。**
	/// 通常ののけぞり(OnFlinch)より深く長く、その間BTを完全に止める。
	/// </summary>
	void OnCriticalReceived(float recoilSeconds);
	/// <summary>仰け反りの姿勢を毎フレーム進める。</summary>
	void UpdateCriticalRecoil(float deltaTime);
	/// <summary>
	/// スタン姿勢をプロシージャルに作る(スキニングが無いのでクリップではなくコードで)。
	/// 全脚を曲線レイヤーへ移して外へ広げ、胴体を沈め、小さく震わせる。終盤で歩行へ戻す。
	/// </summary>
	void UpdateStunPose(float deltaTime);

	/// <summary>
	/// フェーズ共通処理。前回と別フェーズなら開始扱いにし、経過時間を進めて
	/// 0〜1の進捗を outProgress へ返します。durationを超えたらtrue(=フェーズ完了)。
	/// </summary>
	bool TickPhase(const char* phaseName, float duration, float deltaTime, float& outProgress);

public:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT_NAMED_TIP(phase2HealthPercent_, "Phase2 HP", 0.01f, 0.0f, 1.0f,
		    "第2形態へ移るHPの割合。**0にすると第2形態にならない。**\n"
		    "ここを跨いだ瞬間に一度だけ移行し、以後HPが戻っても元へは戻らない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(phase2SpeedScale_, "Phase2 Speed", 0.01f, 0.5f, 3.0f,
		    "第2形態での移動速度の倍率。**近づく速さが上がると一番「激化した」と伝わる。**");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(phase2DamageScale_, "Phase2 Damage", 0.01f, 0.5f, 3.0f, "第2形態での衝撃波のダメージ倍率。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(phase2RadiusScale_, "Phase2 Radius", 0.01f, 0.5f, 3.0f,
		    "第2形態での衝撃波の広がりの倍率。回避の猶予が減るので上げすぎない。");
		KUJATA_REGISTER_VECTOR3_NAMED_TIP(phase2EmissiveColor_, "Phase2 Glow Color", 0.01f, 0.0f, 1.0f,
		    "第2形態での胴体の発光色。**攻撃予兆の点滅と同系色にしないこと**(見分けが付かなくなる)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(phase2EmissiveIntensity_, "Phase2 Glow", 0.01f, 0.0f, 10.0f,
		    "第2形態での発光の強さ。予兆の点滅(既定8)よりはっきり弱くして、常時光と区別できるようにする。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(phase2BurstRadius_, "Phase2 Burst", 0.1f, 0.0f, 40.0f,
		    "移行した瞬間に出す衝撃波の半径。0で出さない。**周りを一度弾いて「変わった」と分からせる。**");
		KUJATA_REGISTER_STRING_NAMED_TIP(targetTag_, "Target Tag",
		    "攻撃対象のタグ。このタグが付いた生存キャラ(PlayerHealth持ち)のうち最寄りを狙う。\n"
		    "名前ではなくタグで選ぶのは、プレイアブル2人+味方NPCの構成に対応するため。");
		KUJATA_REGISTER_STRING_NAMED_TIP(strikeObjectSuffix_, "Strike Object Suffix",
		    "足の攻撃判定オブジェクトの名前の接尾辞。\n"
		    "脚の名前(Leg0等)と繋げて \"Leg0_Strike\" を階層から探し、そのEnemyWeaponをON/OFFする。");
		KUJATA_REGISTER_STRING_NAMED_TIP(beamObjectName_, "Beam Object Name",
		    "ビームの器になる子オブジェクトの名前。\n"
		    "+Z方向へ伸びるCube + トリガーBoxCollider(size 1,1,1) + EnemyWeapon を持たせておく。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(beamHeight_, "Beam Height", 0.01f, 0.0f, 10.0f,
		    "ビームが出る高さ(ルート基準のローカル)。\n"
		    "GuardianBodyの Ride Height と同じ値にすると、胴体の中心から出ているように見える。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(beamMaxLength_, "Beam Max Length", 0.5f, 5.0f, 200.0f,
		    "ビームを地面まで伸ばすときの上限[m]。\n"
		    "**ビームは地面との交点まで自動で伸びる。** ノードのlengthは最短の長さで、\n"
		    "目が高い位置にいるほど実際は長くなる。ここは伸びすぎを止めるための蓋。");
		KUJATA_REGISTER_STRING_NAMED_TIP(treeName_, "Tree Name",
		    "この個体が回すツリーの名前。**同じBTセット(GuardianBT)の中の別ツリーを指せる。**\n"
		    "空なら activeTreeName のツリー。第1形態は \"Guardian\"、第2形態は \"GuardianPhase2\"。\n"
		    "**ライブ監視のキーも兼ねる**ので、BehaviorTree.jsonのツリー名と完全に一致させること。\n"
		    "\n"
		    "形態ごとにBTセットを分けないのは、`SaveToBTSetFolder` がフォルダごとに\n"
		    "FunctionCatalogを1つだけ持つため。別コンポーネントで別セットにすると、\n"
		    "**互いのカタログを上書きし合う**。ノードの登録元は1つに集約する。");
		KUJATA_REGISTER_STRING_NAMED_TIP(beamPrefabPath_, "Beam Prefab",
		    "ビームの器のPrefab。**Beam Object Name の子オブジェクトが無いときにこれから作る。**\n"
		    "生成された器は親を持たないワールド空間の板で、目の向きに合わせてコードが置く。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(arenaRadius_, "Arena Radius", 0.5f, 0.0f, 400.0f,
		    "闘技場の中心(Arena Center)からこの距離までしか出ない。0で無制限。\n"
		    "**突進や飛びかかりは位置を直接書き換える**ので、壁のコライダーでは止まらない。\n"
		    "止めないと闘技場の外まで走り抜け、追いかける味方ごと戦場から出ていく。");
		KUJATA_REGISTER_VECTOR3_NAMED_TIP(arenaCenter_, "Arena Center", 0.5f, -500.0f, 500.0f, "闘技場の中心(ワールド座標)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(orbPrefabPath_, "Orb Prefab",
		    "第2形態の弾(星屑・胞子)の器のPrefab。EnemyWeapon付きの球で、位置だけコードが動かす。\n"
		    "**プールして使い回す**ので、同時に出る最大数ぶんだけ生成される。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(idleEmissiveIntensity_, "Idle Glow", 0.01f, 0.0f, 10.0f,
		    "**通常時**の目の発光。低くしておくと、攻撃前に光ることがそのまま予兆になる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(activeEmissiveIntensity_, "Attack Glow", 0.05f, 0.0f, 20.0f,
		    "攻撃行動中の目の発光。待機との差が大きいほど「動き出した」ことが伝わる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(emissiveFadeRate_, "Glow Fade Rate", 0.1f, 0.1f, 60.0f,
		    "発光が目標値へ寄る速さ[1/秒]。小さいほどゆっくりフェードする。");
		KUJATA_REGISTER_VECTOR3_NAMED_TIP(baseEmissiveColor_, "Base Glow Color", 0.01f, 0.0f, 1.0f,
		    "第1形態の目の発光色。第2形態では Phase2 Glow Color(赤)へ切り替わる。");
		KUJATA_REGISTER_STRING_NAMED_TIP(shockwavePrefabPath_, "Shockwave Prefab",
		    "衝撃刃(足攻撃・コマ回転の広い当たり判定)のPrefab。\n"
		    "脚の先端の球だけでは振り回した軌跡を拾えないので、面の判定をこれで出す。空なら衝撃刃なし。");
		KUJATA_REGISTER_INT_NAMED_TIP(bodyPartLayer_, "Body Part Layer", 1.0f, 0, 31,
		    "ボスの体(ルートと全ての子孫)を置くレイヤー番号。\n"
		    "カメラの Obstacle Mask からこのビットを外すと、脚がカメラを押しのけなくなる。\n"
		    "当たり判定は残るので、攻撃も被弾も従来どおり。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stunSpread_, "Stun Spread", 0.05f, 0.0f, 10.0f,
		    "スタン中に脚を外へ投げ出す距離(ルート中心からの水平距離)。大きいほどへたり込んだ見た目になる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stunSink_, "Stun Sink", 0.05f, 0.0f, 5.0f,
		    "スタン中に胴体を沈める量。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stunTremble_, "Stun Tremble", 0.01f, 0.0f, 1.0f,
		    "スタン中の胴体の震え幅(上下)。0で震えない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(flinchDuration_, "Flinch Duration", 0.05f, 0.0f, 3.0f,
		    "ジャストガードされたときののけぞり秒数。この間は行動しない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(criticalRecoilPitch_, "Critical Recoil Pitch", 0.01f, 0.0f, 1.5f,
		    "致命を受けたときに胴体が反り返る角度[rad]。**ここが大きいほど「効いた」感じになる**。0.5前後が目安。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(criticalRecoilSink_, "Critical Recoil Sink", 0.01f, 0.0f, 3.0f,
		    "致命を受けたときに胴体が持ち上がる量。反り返りと合わせて後ろへ倒れかける形になる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(flinchSink_, "Flinch Sink", 0.05f, 0.0f, 3.0f,
		    "のけぞり時に胴体を沈める量(一瞬沈んで戻る)。");

		KUJATA_REGISTER_FLOAT_NAMED_TIP(flinchTilt_, "Flinch Tilt", 0.5f, 0.0f, 40.0f,
		    "殴られた向きへ胴体を傾ける角度[度]。**当てた側から見て「効いた」と分かる**のはこれ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(flinchPush_, "Flinch Push", 0.01f, 0.0f, 2.0f,
		    "殴られた向きへ目を押し込む距離[m]。傾きと合わせて「押された」感じを作る。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(flinchGuardScale_, "Flinch Guard Scale", 0.05f, 1.0f, 4.0f,
		    "ジャストガードで弾いたときのリアクション倍率。通常のヒットより大きく仰け反らせる。");
	}

private:
	// 攻撃対象のタグ。
	KUJATA_FIELD_STRING(targetTag_, "Ally");
	// 足の攻撃判定オブジェクトの接尾辞。
	KUJATA_FIELD_STRING(strikeObjectSuffix_, "_Strike");
	// ビームの器になる子オブジェクトの名前。
	KUJATA_FIELD_STRING(beamObjectName_, "Beam");
	// ビームが出る高さ(ルート基準ローカル)。GuardianBodyのrideHeight_と揃えてある。
	KUJATA_FIELD_FLOAT(beamHeight_, 1.7f);
	KUJATA_FIELD_FLOAT(beamMaxLength_, 70.0f);
	// 回すツリーの名前(同じBTセット内。空でactiveTreeName)。監視キーも兼ねる。
	KUJATA_FIELD_STRING(treeName_, "Guardian");
	// ビームの器のPrefab(子オブジェクトが無いとき用)。
	KUJATA_FIELD_STRING(beamPrefabPath_, "Prefabs/GuardianBeam.prefab.json");
	// 弾(光球/胞子)の器のPrefab。
	KUJATA_FIELD_STRING(orbPrefabPath_, "Prefabs/GuardianOrb.prefab.json");
	// 闘技場の半径[m](0=無制限)。
	KUJATA_FIELD_FLOAT(arenaRadius_, 0.0f);
	// 闘技場の中心。
	KUJATA_FIELD_VECTOR3(arenaCenter_, (KujataEngine::Vector3{0.0f, 0.0f, 0.0f}));
	// 通常時の目の発光。
	KUJATA_FIELD_FLOAT(idleEmissiveIntensity_, 0.30f);
	// 攻撃中の目の発光。
	KUJATA_FIELD_FLOAT(activeEmissiveIntensity_, 2.0f);
	// 発光のフェード速度[1/s]。
	KUJATA_FIELD_FLOAT(emissiveFadeRate_, 5.0f);
	// 第1形態の発光色。
	KUJATA_FIELD_VECTOR3(baseEmissiveColor_, (KujataEngine::Vector3{1.0f, 0.16f, 0.09f}));
	// 衝撃刃のPrefab。
	KUJATA_FIELD_STRING(shockwavePrefabPath_, "Prefabs/GuardianShockwave.prefab.json");
	// ボスの体を置くレイヤー(カメラの障害物判定から外すため)。
	KUJATA_FIELD_INT(bodyPartLayer_, 8);
	// スタン姿勢: 脚の広げ幅。
	KUJATA_FIELD_FLOAT(stunSpread_, 3.4f);
	// スタン姿勢: 胴体の沈み。
	KUJATA_FIELD_FLOAT(stunSink_, 0.9f);
	// スタン姿勢: 震え幅。
	KUJATA_FIELD_FLOAT(stunTremble_, 0.05f);
	// のけぞり秒数。
	KUJATA_FIELD_FLOAT(flinchDuration_, 0.4f);
	// のけぞり時の沈み。
	KUJATA_FIELD_FLOAT(flinchSink_, 0.35f);
	KUJATA_FIELD_FLOAT(flinchTilt_, 9.0f);
	KUJATA_FIELD_FLOAT(flinchPush_, 0.35f);
	KUJATA_FIELD_FLOAT(flinchGuardScale_, 2.2f);
	// 致命を受けたときの仰け反り。
	KUJATA_FIELD_FLOAT(criticalRecoilPitch_, 0.55f);
	KUJATA_FIELD_FLOAT(criticalRecoilSink_, 0.9f);

	// --- 第2形態 ---
	KUJATA_FIELD_FLOAT(phase2HealthPercent_, 0.5f);
	KUJATA_FIELD_FLOAT(phase2SpeedScale_, 1.35f);
	KUJATA_FIELD_FLOAT(phase2DamageScale_, 1.4f);
	KUJATA_FIELD_FLOAT(phase2RadiusScale_, 1.2f);
	KUJATA_FIELD_VECTOR3(phase2EmissiveColor_, (KujataEngine::Vector3{1.0f, 0.05f, 0.03f}));
	KUJATA_FIELD_FLOAT(phase2EmissiveIntensity_, 1.6f);
	KUJATA_FIELD_FLOAT(phase2BurstRadius_, 14.0f);

	// BT作成用。
	BahamutAI::BehaviorTreeFactory btFactory_;
	// BT実行本体(毎フレームTickする)。
	BahamutAI::BehaviorTreeRuntime btRuntime_;
	// ライブ監視オブザーバー。Playインスタンスだけが遅延生成する。
	std::unique_ptr<BahamutAI::UdpTreeObserver> btObserver_;
	// このボス固有のBlackboard。
	BahamutAI::Blackboard localBlackboard_;

	// --- 攻撃フェーズの実行状態 ---
	// 実行中フェーズ名(空=非攻撃中)。フェーズはRunningをまたぐので、開始済みかをこれで判定する。
	std::string currentPhase_;
	// 現在フェーズの経過時間[s]。
	float phaseTimer_ = 0.0f;
	// 攻撃に使っている脚(-1=なし)。
	int activeLeg_ = -1;
	// 攻撃開始時の足先(ルートローカル)。振り上げの起点。
	KujataEngine::Vector3 attackStartLocal_ = {0.0f, 0.0f, 0.0f};
	// 振り上げ切った位置(ルートローカル)。踏み下ろしの起点になる。
	KujataEngine::Vector3 attackRaisedLocal_ = {0.0f, 0.0f, 0.0f};
	// 踏み下ろし/薙ぎ払いの着弾点(ルートローカル)。フェーズ開始時に対象位置から決める。
	KujataEngine::Vector3 attackImpactLocal_ = {0.0f, 0.0f, 0.0f};

	// --- 飛びかかりの実行状態(こちらはワールド座標。ルート自体が動くため) ---
	// 踏み切り位置。
	KujataEngine::Vector3 leapStartWorld_ = {0.0f, 0.0f, 0.0f};
	// 着地位置。溜めの終わりに対象位置から決めて固定する。
	KujataEngine::Vector3 leapLandWorld_ = {0.0f, 0.0f, 0.0f};

	// --- 弾(光球/胞子)のプール ---
	/// <summary>飛んでいる弾1つぶんの状態。器はPrefabから作って使い回す。</summary>
	struct Orb {
		KujataEngine::GameObject* object = nullptr;
		KujataEngine::Vector3 velocity = {0.0f, 0.0f, 0.0f};
		// 残り寿命[s]。0以下で畳む。
		float lifetime = 0.0f;
		// 着弾後その場に残るか(残留する胞子)。
		bool linger = false;
		// 追尾する相手(nullptrで直進)。**術師の弾と同じ「曲がってくる」挙動**に使う。
		KujataEngine::GameObject* homingTarget = nullptr;
		// 1秒あたりに向きを変えられる角度[rad]。大きいほどしつこく曲がる。
		float homingTurnRate = 0.0f;
		// 既に着弾して残留に移ったか。
		bool landed = false;
		bool active = false;
	};
	std::vector<Orb> orbs_;
	bool orbPrefabFailed_ = false;

	// --- 目の攻撃の実行状態 ---
	// ランタイム生成したビームの器(親を持たないワールド空間の板)。
	KujataEngine::GameObject* beam_ = nullptr;
	bool beamTried_ = false;
	// 第2形態の連続攻撃で「次に何発目か」を数える(星屑・叩きつけ連で使う)。
	int volleyIndex_ = 0;
	// 分離攻撃で目を降ろす前の高さ(合体で戻す先)。
	float eyeMergeStartY_ = 0.0f;
	// **親子付けに戻したときに目が来る高さ。**
	// 合体の行き先をここにしないと、目を y=0(ルート=足元)へ降ろしてから
	// 親子に戻す瞬間に頭の位置へ跳ぶ、という動きになる。
	float eyeAttachedY_ = 0.0f;
	// ビームを振り下ろす狙いのYaw(フェーズ開始時に固定する。追い続けると避けられなくなる)。
	float eyeAimYaw_ = 0.0f;
	// 目の攻撃が出している予告のID。
	int eyeThreatId_ = 0;
	// **目が対象の周りを回る角度[rad]。** 第2形態の目は脚から完全に独立していて、
	// 脚の位置ではなく「対象を挟んで脚の反対側」を自分で目指して回り込む。
	float eyeOrbitAngle_ = 0.0f;
	// 目の速度[m/s]。**位置ではなく速度を積む**ので、狙いが切り替わっても飛ばない。
	KujataEngine::Vector3 eyeVelocity_ = {0.0f, 0.0f, 0.0f};
	// 漂いながら撃つ弾の次発までの残り[s]。
	float eyeSnipeTimer_ = 0.0f;
	// その弾が出している予告のID。
	int eyeSnipeThreatId_ = 0;

	// --- 発光 ---
	// 今フレーム要求された発光の強さ(-1=要求なし→待機値へ戻る)。
	float requestedEmissive_ = -1.0f;
	// 現在の発光の強さ(フェードの実体)。
	float currentEmissive_ = 0.0f;
	// 攻撃フェーズを回している間だけ正になるタイマー[s]。TickPhaseが立てる。
	float attackGlowTimer_ = 0.0f;

	// --- 攻撃予告(ThreatBoard)の実行状態 ---
	// 脚の攻撃が出している予告のID(踏みつけ・薙ぎ払い・飛びかかり・コマ回転で使い回す)。
	int attackThreatId_ = 0;

	// --- ジェット飛行の実行状態 ---
	// 離陸を始めた高さ。降下の目標にも使う。
	float jetGroundY_ = 0.0f;
	// 降下開始時の高度。毎フレーム現在高度から計算し直すと指数的に落ちてしまうので固定する。
	float jetLandStartHeight_ = 0.0f;

	// --- 衝撃刃(広い当たり判定)の実行状態 ---
	// Prefabから生成した器(1体につき1つ。Playインスタンス内なので停止で消える)。
	KujataEngine::GameObject* shockwave_ = nullptr;
	// 生成を試みたか(失敗時に毎フレーム再試行しないため)。
	bool shockwaveTried_ = false;
	// 出ている残り時間[s]。0以下かつ shockwaveHold_ が false なら消える。
	float shockwaveTimer_ = 0.0f;
	// 全体の長さ[s](広がりの補間に使う)。
	float shockwaveDuration_ = 0.0f;
	// 開始半径 / 終了半径。
	float shockwaveStartRadius_ = 0.0f;
	float shockwaveEndRadius_ = 0.0f;
	// 中心(followがfalseのときの固定位置)。
	KujataEngine::Vector3 shockwaveCenter_ = {0.0f, 0.0f, 0.0f};
	// ボスに追従させるか。
	bool shockwaveFollow_ = false;
	// 時間で消えず、StopShockwaveされるまで出しっぱなしにするか。
	bool shockwaveHold_ = false;
	// Prefabのマテリアルが持つ既定色。減衰はこれを基準にαだけ落とす。
	KujataEngine::Vector4 shockwaveBaseColor_ = {1.0f, 1.0f, 1.0f, 1.0f};

	// --- スタン/のけぞりの実行状態 ---
	// 自分のHP/体勢崩し(OnPlayStartで解決)。
	class EnemyHealth* health_ = nullptr;
	// スタン姿勢の経過時間[s]。
	float stunElapsed_ = 0.0f;
	// のけぞりの残り時間[s]。0より大きい間はBTを回さない。
	float flinchTimer_ = 0.0f;
	// のけぞりの強さ(1.0=通常のヒット)。ジャストガードで弾いたときは大きくなる。
	float flinchStrength_ = 1.0f;
	// 殴られた向き(ワールド・水平の単位ベクトル)。押し込みと傾きの向きに使う。
	KujataEngine::Vector3 flinchDir_ = {0.0f, 0.0f, 1.0f};
	// 致命の仰け反りの残り時間と総尺。
	float criticalRecoilTimer_ = 0.0f;
	// 第2形態に入ったか。**一方通行**(HPが回復しても戻らない)。
	bool phase2_ = false;
	// カットシーンが体を預かっている間はtrue(BTを回さない)。
	bool suspended_ = false;
	// 移行の合図として出している波か。この波にだけは第2形態の倍率を掛けない。
	bool emittingPhase2Burst_ = false;
	float criticalRecoilDuration_ = 1.0f;
	// スタン開始時の各脚の足先(ルートローカル)。ここから広げ位置へ補間する。
	std::vector<KujataEngine::Vector3> stunStartLocal_;
};
