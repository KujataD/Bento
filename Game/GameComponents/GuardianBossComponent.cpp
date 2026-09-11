#include "GuardianBossComponent.h"
#include "GameAudio.h"
#include "GameFx.h"
#include <components/ParticleSystemComponent.h>

#include "EnemyHealth.h"
#include "HateTable.h"
#include "EnemyWeapon.h"
#include "GuardianBody.h"
#include "ThreatBoard.h"
#include "GuardianGait.h"
#include "GuardianRigMath.h"
#include "IGuardianLegRig.h"
#include "PlayerHealth.h"

#include <Editor/PrefabAsset.h>
#include <components/ModelRendererComponent.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace KujataEngine;

namespace {

std::string BTSetFolder() { return (KujataEngine::GetProjectDataRoot() / "Resources" / "bt_set" / "GuardianBT").generic_string(); }

constexpr float kDegreeToRadian = std::numbers::pi_v<float> / 180.0f;

// 衝撃波の輪を地面から浮かせる量。板ポリを地面と同じ高さに置くとZファイティングでチラつく。
// 大きくすると今度は浮いて見えるので、チラつきが消える最小限に留める。
constexpr float kShockwaveGroundLift = 0.05f;

// 角度を[-π, π]へ折り返す(KujataEngine::WrapAngleはDLLからリンクできないのでローカルに持つ)。
float WrapYawAngle(float angle) {
	constexpr float pi = std::numbers::pi_v<float>;
	angle = std::fmod(angle + pi, 2.0f * pi);
	if (angle < 0.0f) {
		angle += 2.0f * pi;
	}
	return angle - pi;
}

// 自身または子孫から名前でGameObjectを探す。
GameObject* FindDescendantByName(GameObject* object, const std::string& name) {
	if (!object) {
		return nullptr;
	}
	if (object->GetName() == name) {
		return object;
	}
	for (GameObject* child : object->GetChildren()) {
		if (GameObject* found = FindDescendantByName(child, name)) {
			return found;
		}
	}
	return nullptr;
}

// 両端がなめらかに繋がる補間。振り上げ/復帰の緩急に使う。
float SmoothStep(float t) {
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// 手前が速く終わりが緩む。踏み下ろしの加速感に使う。
float EaseInQuad(float t) {
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t;
}

} // namespace

void GuardianBossComponent::Initialize() {
	// BTのアクション登録。FunctionCatalogへ同時登録するとBahamutAIEditorの一覧に反映される。
	BahamutAI::FunctionCatalog catalog;

	// --- Condition ---

	BahamutAI::RegisterCondition(
	    btFactory_, catalog,
	    BahamutAI::ConditionDef("IsTargetWithin").Category("Sense").Description("対象との水平距離がdistance以内か").Float("distance", {8.0f}, "判定距離(m)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return IsTargetWithin(params);
	    });

	// --- Movement ---

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("MoveToTarget")
	        .Category("Movement")
	        .Description("speed分だけ対象へ向かって進む。speedを負にすると後退する(毎TickSuccess)")
	        .Float("speed", {3.0f}, "移動速度(m/s)。負で後退")
	        .Float("turnSpeed", {2.0f}, "旋回速度(rad/s)。0で旋回しない"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return MoveToTarget(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("FaceTarget").Category("Movement").Description("対象の方向へ旋回する。向き切るまでRunning").Float("turnSpeed", {2.0f}, "旋回速度(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return FaceTarget(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("BodySpin")
	        .Category("Movement")
	        .Description("胴体(球体)だけを回す。脚は接地したまま")
	        .Float("duration", {1.5f}, "回す長さ(s)")
	        .Float("speed", {360.0f}, "回転速度(deg/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return BodySpin(context, params); });

	// --- 足の攻撃。Sequenceで組み合わせて1つの攻撃を構成する ---

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("StompRaise")
	        .Category("Attack")
	        .Description("脚を1本選んで振り上げる。曲線レイヤーへ主導権を移すのでその脚は歩行を止める")
	        .Float("duration", {0.5f}, "振り上げにかける時間(s)")
	        .Float("hold", {0.0f}, "振り上げ切った頂点で静止する時間(s)。**遅延攻撃はここで作る**")
	        .Float("leg", {-1.0f}, "使う脚の番号(0〜3)。-1で対象に最も向いている脚を自動選択")
	        .Float("height", {2.2f}, "振り上げる高さ(m)")
	        .Float("forward", {0.6f}, "振り上げ時に対象側へ寄せる量(m)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return StompRaise(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("StompSlam")
	        .Category("Attack")
	        .Description("対象へ向けて踏み下ろす。攻撃判定ON")
	        .Float("duration", {0.22f}, "フェーズの長さ(s)。短いほど鋭い")
	        .Float("reach", {3.2f}, "ルート中心から踏み込める最大の水平距離(m)")
	        .Float("depth", {0.0f}, "着弾点の高さ(ルート基準)。負で地面へめり込ませる")
	        .Float("waveRadius", {4.0f}, "衝撃刃が広がり切ったときの半径(m)。足そのものより広く取ると当てやすい")
	        .Float("waveStart", {1.0f}, "衝撃刃の開始半径(m)")
	        .Float("waveDuration", {0.35f}, "衝撃刃が広がって消えるまでの時間(s)")
	        .Float("waveDamage", {14.0f}, "衝撃刃のダメージ")
	        .Float("waveKnockback", {9.0f}, "衝撃刃の吹き飛ばし初速")
	        .Float("waveStun", {0.6f}, "衝撃刃で動けなくなる時間(s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return StompSlam(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("StompRecover")
	        .Category("Attack")
	        .Description("脚を歩行へ返す。曲線レイヤーの主導権を戻すと歩行側が自動で踏み直す")
	        .Float("duration", {0.45f}, "フェーズの長さ(s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return StompRecover(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("LegSweep")
	        .Category("Attack")
	        .Description("振り上げた脚を横へ薙ぎ払う。攻撃判定ON。StompRaiseの後に繋ぐ")
	        .Float("duration", {0.5f}, "フェーズの長さ(s)")
	        .Float("radius", {3.0f}, "薙ぎ払う円弧の半径(m)")
	        .Float("arc", {150.0f}, "薙ぎ払う角度(deg)")
	        .Float("height", {0.9f}, "薙ぎ払う高さ(ルート基準, m)")
	        .Float("waveRadius", {4.5f}, "衝撃刃が広がり切ったときの半径(m)。足そのものより広く取ると当てやすい")
	        .Float("waveStart", {1.5f}, "衝撃刃の開始半径(m)")
	        .Float("waveDuration", {0.3f}, "衝撃刃が広がって消えるまでの時間(s)")
	        .Float("waveDamage", {12.0f}, "衝撃刃のダメージ")
	        .Float("waveKnockback", {11.0f}, "衝撃刃の吹き飛ばし初速")
	        .Float("waveStun", {0.6f}, "衝撃刃で動けなくなる時間(s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return LegSweep(context, params); });

	// --- 飛びかかり。遠距離から一気に詰めて全脚で踏み潰す ---

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("LeapCharge")
	        .Category("Leap")
	        .Description("飛びかかりの溜め。沈み込みつつ対象を向き、着地点を決める")
	        .Float("duration", {0.7f}, "溜めの長さ(s)")
	        .Float("sink", {0.7f}, "沈み込む深さ(m)")
	        .Float("turnSpeed", {3.0f}, "旋回速度(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return LeapCharge(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("LeapFly")
	        .Category("Leap")
	        .Description("放物線で着地点まで飛ぶ。4本とも脚を畳む")
	        .Float("duration", {0.85f}, "滞空時間(s)")
	        .Float("height", {6.0f}, "跳躍の高さ(m)")
	        .Float("range", {16.0f}, "跳べる最大の水平距離(m)")
	        .Float("stopDistance", {2.4f}, "対象の手前で止まる距離(m)。0だと真上へ落ちる")
	        .Float("tuck", {0.45f}, "脚を内側へ引き寄せる割合(0〜1)")
	        .Float("lift", {1.2f}, "脚を持ち上げる高さ(m)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return LeapFly(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("LeapSlam")
	        .Category("Leap")
	        .Description("着地。4本すべてを外へ叩きつける。全脚の攻撃判定ON")
	        .Float("duration", {0.18f}, "叩きつけの長さ(s)。短いほど鋭い")
	        .Float("spread", {3.6f}, "叩きつける水平距離(m)")
	        .Float("depth", {-0.1f}, "叩きつける高さ(ルート基準, m)。負で地面へめり込む")
	        .Float("waveRadius", {6.0f}, "衝撃刃が広がり切ったときの半径(m)。足そのものより広く取ると当てやすい")
	        .Float("waveStart", {1.5f}, "衝撃刃の開始半径(m)")
	        .Float("waveDuration", {0.4f}, "衝撃刃が広がって消えるまでの時間(s)")
	        .Float("waveDamage", {18.0f}, "衝撃刃のダメージ")
	        .Float("waveKnockback", {12.0f}, "衝撃刃の吹き飛ばし初速")
	        .Float("waveStun", {0.6f}, "衝撃刃で動けなくなる時間(s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return LeapSlam(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("LeapRecover").Category("Leap").Description("全脚を歩行へ返す").Float("duration", {0.5f}, "フェーズの長さ(s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return LeapRecover(context, params); });

	// --- ジェット飛行。胴体下部のエンジンで低空を浮遊し、脚を高速回転させて追い回す ---

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("JetWarn")
	        .Category("Jet")
	        .Description("コマ回転の予兆。沈み込んで脚を抱え込み、胴体を点滅させながらゆっくり回り始める(判定なし)")
	        .Float("duration", {1.2f}, "予兆の長さ(s)。長いほど避ける猶予が増える")
	        .Float("sink", {0.6f}, "沈み込む量(m)")
	        .Float("tuck", {0.35f}, "脚を内側へ引き寄せる割合(0〜1)")
	        .Float("lift", {0.5f}, "脚を持ち上げる高さ(m)")
	        .Float("spinSpeed", {240.0f}, "予兆中の回転速度(deg/s)。本番より遅くして「溜め」に見せる")
	        .Float("flashHz", {6.0f}, "胴体の点滅の速さ(回/秒)")
	        .Float("flashIntensity", {8.0f}, "点滅の明るさ"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return JetWarn(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("JetLiftOff")
	        .Category("Jet")
	        .Description("コマの構えへ移る。脚を外へ突っ張らせ、胴体ごと回し始める")
	        .Float("duration", {0.6f}, "構えに移る時間(s)")
	        .Float("height", {-0.7f}, "地面からルートまでの高さ(m)。**負にすると胴体が地面に接地する**")
	        .Float("spinReach", {1.0f}, "脚を伸ばす割合(最大長に対する。1.0でピンと伸ばし切る)")
	        .Float("spinPitch", {0.0f}, "脚の傾き(deg)。0で地面と平行")
	        .Float("spinSpeed", {540.0f}, "**胴体ごと**回る速度(deg/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return JetLiftOff(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("JetChase")
	        .Category("Jet")
	        .Description("コマのまま対象へ寄っていく。回転中は全脚の攻撃判定ON")
	        .Float("duration", {5.0f}, "回り続ける時間(s)")
	        .Float("height", {-0.7f}, "地面からルートまでの高さ(m)。**負にすると胴体が地面に接地する**")
	        .Float("speed", {2.5f}, "接近速度(m/s)。向きは変えず位置だけ寄せる。速すぎると当たる前に通り過ぎる")
	        .Float("spinReach", {1.0f}, "脚を伸ばす割合(最大長に対する。1.0でピンと伸ばし切る)")
	        .Float("spinPitch", {0.0f}, "脚の傾き(deg)。0で地面と平行")
	        .Float("spinSpeed", {900.0f}, "**胴体ごと**回る速度(deg/s)")
	        .Float("strikeScale", {2.0f}, "回転中だけ脚の攻撃判定を太らせる倍率。高速で振り回すとすり抜けるため")
	        .Float("waveRadius", {4.5f}, "**掃いている円盤そのものの判定**の半径(m)。脚の先端だけでは輪の内側に入られると当たらない")
	        .Float("waveHeight", {0.8f}, "円盤の判定の高さ(地面から, m)")
	        .Float("waveDamage", {16.0f}, "轢いたときのダメージ")
	        .Float("waveKnockback", {12.0f}, "轢いたときの吹き飛ばし初速")
	        .Float("waveStun", {0.7f}, "轢かれて動けなくなる時間(s)")
	        .Float("waveHitInterval", {0.7f}, "同じ相手へ轢き直すまでの間隔(s)。短いと乗られただけで即死する"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return JetChase(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("JetLand")
	        .Category("Jet")
	        .Description("回転を落として着地し、脚を歩行へ返す")
	        .Float("duration", {0.6f}, "着地にかける時間(s)")
	        .Float("spinReach", {1.0f}, "脚を伸ばす割合(最大長に対する)")
	        .Float("spinPitch", {0.0f}, "脚の傾き(deg)。0で地面と平行")
	        .Float("spinSpeed", {360.0f}, "着地までの回転速度(deg/s)。落としていくと止まって見える"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return JetLand(context, params); });

	// --- ビーム。脚を使わない遠距離攻撃。溜め→照射→消灯の3段 ---

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("BeamCharge")
	        .Category("Beam")
	        .Description("ビームの予告。**点滅**しながら対象を向く。当たり判定は出さない")
	        .Float("duration", {1.2f}, "点滅で予告する長さ(s)。ここが短いと理不尽な攻撃になる")
	        .Float("blinkInterval", {0.12f}, "点滅の間隔(s)。小さいほど速く瞬く")
	        .Float("turnSpeed", {2.5f}, "旋回速度(rad/s)。予告中に正面を取り切る")
	        .Float("length", {14.0f}, "ビームの長さ(ルート基準ローカル)。ルートのscaleが2なら見た目は倍")
	        .Float("thickness", {0.35f}, "点滅させる太さ。照射時と同じにすると「点滅か実体か」だけの差になる")
	        .Float("aimHeight", {1.0f}, "対象のどの高さを狙うか(m)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return BeamCharge(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("BeamFire")
	        .Category("Beam")
	        .Description("ビームを照射する。攻撃判定ON。turnSpeedを落とすと薙ぎ払いになり、避ける余地が生まれる")
	        .Float("duration", {1.0f}, "照射し続ける時間(s)")
	        .Float("turnSpeed", {0.8f}, "旋回速度(rad/s)。**ここが避けられるかどうかを決める**")
	        .Float("length", {14.0f}, "ビームの長さ(ルート基準ローカル)")
	        .Float("thickness", {0.35f}, "ビームの太さ")
	        .Float("aimHeight", {1.0f}, "対象のどの高さを狙うか(m)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return BeamFire(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("BeamRecover")
	        .Category("Beam")
	        .Description("ビームを細めて消す。攻撃判定OFF")
	        .Float("duration", {0.15f}, "消えるまでの時間(s)。短いほど切れ味が出る")
	        .Float("length", {14.0f}, "消える間のビームの長さ(ルート基準ローカル)")
	        .Float("thickness", {0.35f}, "消え始めの太さ。BeamFireと同じ値にする")
	        .Float("aimHeight", {1.0f}, "対象のどの高さを狙うか(m)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return BeamRecover(context, params); });

	// --- 目(Eye)の攻撃 ---
	// **脚を一切使わない。** 目は土台から完全に分離しているので、これらは歩行にも接合部にも触らない。

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("EyeAimUp")
	        .Category("Eye")
	        .Description("目を真上へ向けてビームを細く点滅させる予兆。判定は出さない")
	        .Float("duration", {1.2f}, "予兆の長さ(s)")
	        .Float("blinkInterval", {0.12f}, "点滅の間隔(s)")
	        .Float("length", {14.0f}, "ビームの長さ")
	        .Float("thickness", {0.35f}, "ビームの太さ")
	        .Float("turnSpeed", {2.5f}, "この間に対象を向く速さ(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return EyeAimUp(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("EyeBeamSlam")
	        .Category("Eye")
	        .Description("真上のビームを対象へ振り下ろす。判定ON。着弾点に衝撃波")
	        .Float("duration", {0.5f}, "振り下ろしにかける時間(s)")
	        .Float("length", {14.0f}, "ビームの長さ")
	        .Float("thickness", {0.35f}, "ビームの太さ")
	        .Float("waveRadius", {4.5f}, "着弾の衝撃波の半径")
	        .Float("waveDamage", {14.0f}, "衝撃波のダメージ")
	        .Float("waveKnockback", {10.0f}, "衝撃波のノックバック")
	        .Float("waveStun", {0.6f}, "衝撃波のスタン秒数"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return EyeBeamSlam(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("EyeBeamSpin")
	        .Category("Eye")
	        .Description("ビームを出したまま目を回して薙ぐ。全周が攻撃範囲になる")
	        .Float("duration", {2.07f}, "回す長さ(s)")
	        .Float("spinSpeed", {200.0f}, "回転速度(deg/s)。速すぎると避けられない")
	        .Float("length", {14.0f}, "ビームの長さ")
	        .Float("thickness", {0.35f}, "ビームの太さ")
	        .Float("pitch", {6.0f}, "地面へ向ける俯角(deg)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return EyeBeamSpin(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("EyeDetach")
	        .Category("Eye")
	        .Description("目を地面へ降ろし、脚をすべて宙へ持ち上げて切り離す")
	        .Float("duration", {0.9f}, "切り離しにかける時間(s)")
	        .Float("eyeHeight", {0.9f}, "降りきったときの目の高さ(地面から)")
	        .Float("legLift", {2.6f}, "脚を持ち上げる高さ")
	        .Float("legSpread", {2.6f}, "脚を広げる距離"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return EyeDetach(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("EyeMerge")
	        .Category("Eye")
	        .Description("目を持ち上げて脚を接地へ戻す(合体)")
	        .Float("duration", {0.8f}, "合体にかける時間(s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return EyeMerge(context, params); });

	// --- 第2形態(空中戦)。同じBTセットの別ツリー "GuardianPhase2" から使う ---
	// **カタログはフォルダに1つ**なので、形態が増えても登録元はこの1箇所に集約する。

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("EyeHover")
	        .Category("Phase2")
	        .Description("目を切り離したまま指定の高さに浮かべ、対象の手前へゆっくり漂わせる(毎TickSuccess)")
	        .Float("height", {6.5f}, "地面からの高さ(m)")
	        .Float("lead", {4.0f}, "対象へどれだけ寄せた位置に浮かぶか(m)")
	        .Float("follow", {2.0f}, "漂う速さ[1/秒]。大きいほど機敏に付いてくる")
	        .Float("turnSpeed", {1.6f}, "ルートの旋回速度(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return EyeHover(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("LegSpinChase")
	        .Category("Phase2")
	        .Description("脚を少し広げ、接地したまま回りながら追いかける(薙ぎ払いの置き換え)")
	        .Float("windup", {1.0f}, "脚を開く溜めの秒数")
	        .Float("duration", {3.2f}, "回りながら追う秒数")
	        .Float("spread", {1.35f}, "脚を外へ開く倍率。1.0で定位置")
	        .Float("spinSpeed", {200.0f}, "回転速度(度/秒)")
	        .Float("speed", {2.6f}, "詰める速さ(m/s)")
	        .Float("stopDistance", {2.5f}, "この距離まで来たら詰めるのをやめる")
	        .Float("waveRadius", {5.0f}, "付いて回る刃の半径(m)")
	        .Float("waveDamage", {7.0f}, "刃のダメージ")
	        .Float("waveKnockback", {9.0f}, "刃のノックバック")
	        .Float("waveStun", {0.5f}, "刃のスタン秒")
	        .Float("waveHitInterval", {0.8f}, "同じ相手に連続で当たる間隔(秒)")
	        .Float("strikeScale", {2.0f}, "脚先の判定の太さ")
	        .Float("turnSpeed", {2.2f}, "溜め中の旋回速度(rad/s)")
	        .Float("openSeconds", {2.0f}, "終わったあとの隙(秒)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return LegSpinChase(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("EyeHomingVolley")
	        .Category("Phase2")
	        .Description("追尾弾をばら撒く(術師の弾と同じ挙動)。置き型の胞子の置き換え")
	        .Float("windup", {1.1f}, "溜めの秒数")
	        .Float("fire", {1.4f}, "撃ち切るまでの秒数")
	        .Float("count", {5.0f}, "発射数")
	        .Float("speed", {11.0f}, "弾速(m/s)")
	        .Float("spread", {0.45f}, "初速を横へ散らす量。0で真っ直ぐ")
	        .Float("turnRate", {1.6f}, "曲がれる速さ[1/秒]。大きいほどしつこい")
	        .Float("radius", {0.8f}, "弾の大きさ")
	        .Float("life", {4.0f}, "弾の寿命(秒)")
	        .Float("damage", {6.0f}, "1発のダメージ")
	        .Float("turnSpeed", {2.0f}, "溜め中の旋回速度(rad/s)")
	        .Float("openSeconds", {2.0f}, "終わったあとの隙(秒)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return EyeHomingVolley(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("EyeLaserBurst")
	        .Category("Phase2")
	        .Description("短いビームを狙い直しながら連射する。振り下ろし・全周薙ぎとは別種")
	        .Float("windup", {0.9f}, "最初の狙い付けの秒数")
	        .Float("shots", {3.0f}, "連射数")
	        .Float("shotTime", {0.55f}, "1発ぶんの秒数(前半が狙い付け、後半が照射)")
	        .Float("length", {40.0f}, "ビームの最短の長さ。地面まで自動で伸びる")
	        .Float("thickness", {0.4f}, "ビームの太さ")
	        .Float("pitch", {6.0f}, "撃ち下ろす角度(度)")
	        .Float("damage", {8.0f}, "ダメージ")
	        .Float("openSeconds", {2.0f}, "終わったあとの隙(秒)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return EyeLaserBurst(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("LegsCrawl")
	        .Category("Phase2")
	        .Description("脚だけで対象へ這い寄る。目は浮いたまま(毎TickSuccess)")
	        .Float("speed", {3.0f}, "移動速度(m/s)")
	        .Float("stopDistance", {6.0f}, "この距離まで近づいたら止まる"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return LegsCrawl(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("EyeStarfall")
	        .Category("Phase2")
	        .Description("**目が縮む**溜めのあと、光球を順に降らせる。落ちてくる球そのものが合図")
	        .Float("windup", {1.1f}, "溜めの長さ(s)。**ここが見切りの猶予**")
	        .Float("fire", {1.6f}, "降らせている時間(s)")
	        .Float("count", {8.0f}, "球の数")
	        .Float("spread", {7.0f}, "対象の周りへ散らす半径(m)")
	        .Float("height", {14.0f}, "球を落とす高さ(m)。高いほど滞空が長く避けやすい")
	        .Float("radius", {1.1f}, "球の大きさ")
	        .Float("damage", {12.0f}, "1発のダメージ")
	        .Float("turnSpeed", {2.0f}, "溜め中の旋回速度(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return EyeStarfall(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("EyeSporeBurst")
	        .Category("Phase2")
	        .Description("**目が膨らむ**溜めのあと、胞子を放物線でばら撒く。着弾点はしばらく危険域として残る")
	        .Float("windup", {1.0f}, "溜めの長さ(s)")
	        .Float("recover", {0.6f}, "撒いたあとの隙(s)")
	        .Float("count", {7.0f}, "胞子の数")
	        .Float("speed", {11.0f}, "水平の初速(m/s)")
	        .Float("lift", {6.0f}, "上向きの初速(m/s)")
	        .Float("linger", {5.0f}, "着弾後に残る秒数。**位置取りを崩すのが役目**")
	        .Float("radius", {1.4f}, "胞子の大きさ")
	        .Float("threatRadius", {8.0f}, "味方AIへ伝える危険域の半径(画面には出ない)")
	        .Float("damage", {10.0f}, "接触ダメージ")
	        .Float("turnSpeed", {1.5f}, "溜め中の旋回速度(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return EyeSporeBurst(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("LegCharge")
	        .Category("Phase2")
	        .Description("**脚を後ろへ引き絞る**溜めのあと、地を這って直線に突進する。突進中は追尾しない")
	        .Float("windup", {0.85f}, "引き絞りの長さ(s)。**ここで向きが見える**")
	        .Float("dash", {0.7f}, "突進している時間(s)")
	        .Float("speed", {22.0f}, "突進速度(m/s)")
	        .Float("pull", {1.2f}, "溜めで脚を後ろへ引く量")
	        .Float("reach", {1.6f}, "突進中に脚を前へ投げ出す量")
	        .Float("legHeight", {0.4f}, "突進中の脚の高さ")
	        .Float("sink", {0.9f}, "溜めで胴体を沈める量")
	        .Float("width", {3.0f}, "味方AIへ伝える帯の半幅(画面には出ない)")
	        .Float("strikeScale", {2.0f}, "突進中の脚の判定倍率")
	        .Float("damage", {18.0f}, "接触ダメージ")
	        .Float("turnSpeed", {3.0f}, "溜め中の旋回速度(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return LegCharge(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("CrossfireCombo")
	        .Category("Phase2")
	        .Description("**目の縮みと脚の引き絞りが同時に見える**溜めのあと、ビーム薙ぎと突進を同時に出す")
	        .Float("windup", {1.3f}, "溜めの長さ(s)。2つの溜めが同時に見えることが合図")
	        .Float("duration", {2.2f}, "本番の長さ(s)")
	        .Float("arc", {150.0f}, "ビームを薙ぐ角度(deg)")
	        .Float("length", {18.0f}, "ビームの長さ(m)")
	        .Float("thickness", {0.5f}, "ビームの太さ")
	        .Float("pitch", {8.0f}, "ビームの俯角(deg)")
	        .Float("speed", {12.0f}, "脚の突進速度(m/s)")
	        .Float("pull", {1.1f}, "溜めで脚を引く量")
	        .Float("reach", {1.5f}, "突進中に脚を前へ出す量")
	        .Float("legHeight", {0.4f}, "突進中の脚の高さ")
	        .Float("sink", {0.8f}, "溜めで胴体を沈める量")
	        .Float("width", {3.2f}, "味方AIへ伝える突進の半径(画面には出ない)")
	        .Float("strikeScale", {2.0f}, "脚の判定倍率")
	        .Float("damage", {16.0f}, "突進の接触ダメージ")
	        .Float("turnSpeed", {2.5f}, "溜め中の旋回速度(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return CrossfireCombo(context, params); });

	catalog.SaveToBTSetFolder(BTSetFolder());
}

void GuardianBossComponent::OnPlayStart() {
	LoadBTSet();

	if (!btObserver_) {
		// **キーはツリー名と完全に一致させること。**
		// エディタは購読要求に「ツリー名のハッシュ」を載せて送ってくる。ここが違うと
		// UdpTreeObserver::ShouldTransmit() が常にfalseになり、監視をONにしても
		// 一切パケットが飛ばない(エラーも出ないので気づきにくい)。
		// 形態ごとにツリーが違うので、Tree Name をそのまま監視キーにする。
		btObserver_ = std::make_unique<BahamutAI::UdpTreeObserver>(treeName_.empty() ? std::string("Guardian") : treeName_);
	}

	currentPhase_.clear();
	phaseTimer_ = 0.0f;
	stunElapsed_ = 0.0f;
	// **形態は必ず戻す。** リトライで2回目に入ったとき、最初から第2形態のボスと戦うことになる。
	phase2_ = false;
	emittingPhase2Burst_ = false;
	// 致命の仰け反りも必ず解く。演出の途中でPlayを止めると、次のPlayが
	// 仰け反ったまま始まってBTが一切回らなくなる(ボスが棒立ちになる)。
	criticalRecoilTimer_ = 0.0f;
	SetBodyPitchOffset(0.0f);
	// カメラが脚に反応しないよう、体ごと専用レイヤーへ隔離する(当たり判定は残る)。
	ApplyBodyPartLayer();

	flinchTimer_ = 0.0f;
	stunStartLocal_.clear();
	// 衝撃刃はPlayインスタンスごとに作り直す(前回Playのポインタは無効)。
	shockwave_ = nullptr;
	shockwaveTried_ = false;
	shockwaveTimer_ = 0.0f;
	shockwaveHold_ = false;
	// ビームと弾の器もPlayインスタンスごとに作り直す(前回Playのポインタは無効)。
	beam_ = nullptr;
	beamTried_ = false;
	orbs_.clear();
	orbPrefabFailed_ = false;
	volleyIndex_ = 0;
	eyeAimYaw_ = 0.0f;
	eyeMergeStartY_ = 0.0f;
	eyeAttachedY_ = 0.0f;
	eyeOrbitAngle_ = 0.0f;
	eyeVelocity_ = {0.0f, 0.0f, 0.0f};
	eyeSnipeTimer_ = 0.0f;
	eyeSnipeThreatId_ = 0;
	// 発光は待機の明るさから始める。
	requestedEmissive_ = -1.0f;
	currentEmissive_ = idleEmissiveIntensity_;
	AbortAttack();

	// 体勢崩し(スタン)とのけぞりはEnemyHealthから通知を受ける。
	health_ = GetComponent<EnemyHealth>();
	if (health_) {
		health_->SetOnStagger([this]() { OnStaggered(); });
		health_->SetOnStaggerEnd([this]() { OnStaggerEnd(); });
		health_->SetOnFlinch([this]() { OnFlinch(); });
		// ダメージが通るたびに見た目のリアクションを返す(行動は止めない)。
		health_->SetOnHit([this](GameObject* attacker, float) { OnHitReaction(attacker, 1.0f); });
		health_->SetOnCritical([this](float seconds) { OnCriticalReceived(seconds); });
	}
}

namespace {

// 自分と全ての子孫のレイヤーを揃える。
void SetLayerRecursive(GameObject* object, uint32_t layer) {
	if (!object) {
		return;
	}
	object->SetLayer(layer);
	for (GameObject* child : object->GetChildren()) {
		SetLayerRecursive(child, layer);
	}
}

} // namespace

void GuardianBossComponent::ApplyBodyPartLayer() {
	if (!owner_) {
		return;
	}
	uint32_t layer = static_cast<uint32_t>(std::clamp(bodyPartLayer_, 0, 31));
	SetLayerRecursive(owner_, layer);
}

void GuardianBossComponent::OnStaggered() {
	// 体勢を崩された瞬間はリアクションを畳む。スタンの姿勢と混ざると二重に傾く。
	flinchTimer_ = 0.0f;
	if (GuardianBody* body = GetComponent<GuardianBody>()) {
		body->ClearReactionPose();
	}
	// 攻撃を中断し、実行中の分岐を捨てる(スタン明けに途中から再開しないように)。
	AbortAttack();
	// **スタンは最大の隙。** 味方AIはここで一気に攻め込む(致命もこの窓で入る)。
	Threat::SetOpen(owner_, health_ ? health_->GetStunDuration() : 3.0f);
	if (btRuntime_.IsLoaded()) {
		btRuntime_.Reset();
	}
	flinchTimer_ = 0.0f;
	flinchStrength_ = 1.0f;
	flinchDir_ = {0.0f, 0.0f, 1.0f};
	if (GuardianBody* body = GetComponent<GuardianBody>()) {
		body->ClearReactionPose();
	}
	stunElapsed_ = 0.0f;

	// 今の足先位置から広げ位置へ補間するため、開始時の位置を覚える。
	stunStartLocal_.clear();
	if (IGuardianLegRig* rig = GetRig()) {
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			stunStartLocal_.push_back(ToRootLocal(rig->GetFootWorld(index)));
		}
	}
}

void GuardianBossComponent::OnStaggerEnd() {
	// 全脚を歩行へ返し(AbortAttackはactiveLeg_しか戻さない)、沈みも戻す。
	if (IGuardianLegRig* rig = GetRig()) {
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveWeight(index, 0.0f);
		}
	}
	AbortAttack();
	stunStartLocal_.clear();
}

void GuardianBossComponent::OnCriticalReceived(float recoilSeconds) {
	// **致命を受けたら攻撃を丸ごと畳む。** 仰け反っている最中に攻撃判定が残っていると、
	// 決めたはずの一撃で相討ちになる。
	StopShockwave();
	SetAllStrikesActive(false);
	btRuntime_.Reset();

	criticalRecoilTimer_ = (recoilSeconds > 0.0f) ? recoilSeconds : 1.5f;
	criticalRecoilDuration_ = criticalRecoilTimer_;
	// 通常ののけぞりは打ち消す(二重に沈まないように)。
	flinchTimer_ = 0.0f;
}

void GuardianBossComponent::UpdateCriticalRecoil(float deltaTime) {
	if (criticalRecoilTimer_ <= 0.0f) {
		return;
	}
	criticalRecoilTimer_ -= deltaTime;
	float remain = (std::max)(criticalRecoilTimer_, 0.0f);
	float t = 1.0f - remain / (std::max)(criticalRecoilDuration_, 1.0e-3f);

	// **前半で一気に反り返り、後半でゆっくり戻る。**
	// 対称な山形にすると「軽く揺れた」だけに見えるので、立ち上がりを鋭くして
	// 戻りを長く取り、巨体が持ち直すのに時間がかかっている、という見え方にする。
	float shape = (t < 0.25f) ? (t / 0.25f) : std::pow(1.0f - (t - 0.25f) / 0.75f, 1.6f);
	shape = std::clamp(shape, 0.0f, 1.0f);

	// 上を向いて反り返る(+Z前方の左手系ではX軸の負回転が上向き)。
	SetBodyPitchOffset(-criticalRecoilPitch_ * shape);
	SetBodySink(-criticalRecoilSink_ * shape);

	if (criticalRecoilTimer_ <= 0.0f) {
		criticalRecoilTimer_ = 0.0f;
		SetBodyPitchOffset(0.0f);
		SetBodySink(0.0f);
	}
}

void GuardianBossComponent::OnFlinch() {
	// ジャストガードで弾かれたときの大きめのリアクション。
	// **ここで行動を中断してはいけない。**
	// 以前は AbortAttack + BTのReset をしていたが、そうすると殴られている間ずっと
	// 技が振り出しに戻り、当たる瞬間に一度も到達しなかった。
	// 中断してよいのは体勢崩し(スタン)だけ、という切り分けにしてある。
	OnHitReaction(nullptr, flinchGuardScale_);
}

void GuardianBossComponent::OnHitReaction(GameObject* attacker, float strength) {
	if (!owner_ || (health_ && (!health_->IsAlive() || health_->IsStaggered()))) {
		return;
	}

	// 殴られた向き = 殴ってきた側から自分へ向かうベクトル。押し込む向きでもある。
	Vector3 direction = flinchDir_;
	if (attacker) {
		Vector3 away = owner_->GetTransform().translation_ - attacker->GetTransform().translation_;
		away.y = 0.0f;
		if (Length(away) > 0.0001f) {
			direction = GuardianRigMath::SafeNormalize(away, flinchDir_);
		}
	} else {
		// 相手が分からない場合は正面から受けたことにする(ジャストガードはこの経路)。
		float yaw = GetRootYaw();
		direction = {-std::sin(yaw), 0.0f, -std::cos(yaw)};
	}

	// 連打されたときは、向きを新しい方へ寄せつつ強さを取り直す。
	// 完全に上書きすると小刻みに向きが飛ぶので、少し前の向きを引きずらせる。
	flinchDir_ = GuardianRigMath::SafeNormalize(GuardianRigMath::Lerp3(flinchDir_, direction, 0.6f), direction);
	flinchStrength_ = (std::max)(strength, flinchTimer_ > 0.0f ? flinchStrength_ * 0.6f : 0.0f);
	flinchTimer_ = flinchDuration_;
}

void GuardianBossComponent::UpdateHitReaction(float deltaTime) {
	GuardianBody* body = GetComponent<GuardianBody>();
	if (!body) {
		return;
	}
	if (flinchTimer_ <= 0.0f) {
		body->ClearReactionPose();
		return;
	}

	flinchTimer_ = (std::max)(flinchTimer_ - deltaTime, 0.0f);

	// **立ち上がりは一瞬、戻りはゆっくり。** 等速で戻すとゴムのように見える。
	// 残り時間の二乗にすると、殴られた瞬間にどんと入って素早く収まる手触りになる。
	float t = flinchTimer_ / (std::max)(flinchDuration_, 1.0e-3f);
	float weight = t * t * flinchStrength_;

	// 殴られた向きを、自分の前後(pitch)と左右(roll)へ分解する。
	// こうしておくと**どちら側から殴ったかが傾きで読める**。
	float yaw = GetRootYaw();
	Vector3 forward = {std::sin(yaw), 0.0f, std::cos(yaw)};
	Vector3 right = {std::cos(yaw), 0.0f, -std::sin(yaw)};
	float alongForward = flinchDir_.x * forward.x + flinchDir_.z * forward.z;
	float alongRight = flinchDir_.x * right.x + flinchDir_.z * right.z;

	float tilt = flinchTilt_ * kDegreeToRadian * weight;
	float push = flinchPush_ * weight;

	// 目を押し込む向きは、目が分離しているか(第2形態)で空間が変わる。
	Vector3 offset = flinchDir_ * push;
	if (!body->IsEyeDetachedObject()) {
		// 親子のままの目はルート基準ローカル。ルートのYawぶんを戻してから渡す。
		offset = {alongRight * push, 0.0f, alongForward * push};
	}

	body->SetReactionPose(offset, -flinchSink_ * weight, alongForward * tilt, -alongRight * tilt);
}

void GuardianBossComponent::UpdateStunPose(float deltaTime) {
	IGuardianLegRig* rig = GetRig();
	if (!rig || !health_) {
		return;
	}
	stunElapsed_ += deltaTime;

	float total = (std::max)(health_->GetStunDuration(), 1.0e-3f);
	float remaining = health_->GetStunRemaining();

	// 入り: 0.25秒で崩れ落ちる / 出: 最後の0.5秒で起き上がる(weightを戻して歩行へ引き継ぐ)。
	float collapse = SmoothStep(stunElapsed_ / 0.25f);
	float riseWindow = (std::min)(0.5f, total * 0.3f);
	float rise = remaining < riseWindow ? SmoothStep(1.0f - remaining / riseWindow) : 0.0f;

	int legCount = rig->GetLegCount();
	for (int index = 0; index < legCount; ++index) {
		Vector3 from = (index < static_cast<int>(stunStartLocal_.size())) ? stunStartLocal_[index] : rig->GetHomeLocal(index);
		Vector3 to = SpreadLocal(index, stunSpread_, 0.0f);
		rig->SetCurveWeight(index, 1.0f - rise);
		rig->SetCurveTargetLocal(index, GuardianRigMath::Lerp3(from, to, collapse));
	}

	// 胴体: 沈めて小さく震わせる。起き上がりで戻す。
	float tremble = stunTremble_ * std::sin(stunElapsed_ * 28.0f) * (1.0f - rise);
	SetBodySink((-stunSink_ * collapse + tremble) * (1.0f - rise));
}

void GuardianBossComponent::RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) {
	registry.Add("LoadBTSet", [this]() { LoadBTSet(); });
}

void GuardianBossComponent::SetSuspended(bool suspended) {
	if (suspended_ == suspended) {
		return;
	}
	suspended_ = suspended;
	if (!suspended_) {
		return;
	}
	// 預ける瞬間に、出しっぱなしの判定と予告をすべて畳む。
	// 残したまま止めると、動かないボスの周りに当たり判定だけが浮いたままになる。
	AbortAttack();
	ClearOrbs();
	if (btRuntime_.IsLoaded()) {
		btRuntime_.Reset();
	}
}

void GuardianBossComponent::LoadBTSet() {
	// **同じBTセットの中からツリーを名前で選ぶ。** 形態ごとにフォルダを分けないのは、
	// FunctionCatalogがフォルダに1つしか無く、分けると互いに上書きしてしまうため。
	if (!btRuntime_.LoadFromBTSetFolder(BTSetFolder(), btFactory_, treeName_)) {
		const BahamutAI::BehaviorTreeLoadResult& result = btRuntime_.GetLastLoadResult();
		KujataEngine::Logger::Log(
		    std::string("[GuardianBossComponent] BT load failed (") + treeName_ + "): " + result.GetErrorMessage());
	}
}

void GuardianBossComponent::Update() {
	if (!owner_ || !btRuntime_.IsLoaded()) {
		return;
	}

	float deltaTime = Time::GetDeltaTime();

	// **BTの状態に関わらず見る。** スタン中に閾値を割ることが多いので、
	// 攻撃中しか判定しない作りにすると移行を取りこぼす。
	UpdatePhaseTransition();

	// 衝撃刃はBTの状態に関わらず進める(スタンで攻撃が中断されても、出ている刃は自分で畳む)。
	UpdateShockwave(deltaTime);

	// 弾も同じ理由でBTの外で進める。撃った本人がスタンしても、飛んでいる球は飛び続ける。
	UpdateOrbs(deltaTime);

	// **闘技場から出さない。** 突進・飛びかかり・コマ回転はどれも translation を直接書くので、
	// 壁のコライダーでは止まらない。止めないと外まで走り抜け、追う味方ごと戦場から出ていく。
	if (arenaRadius_ > 0.0f && owner_) {
		Vector3 offset = owner_->GetTransform().translation_ - arenaCenter_;
		offset.y = 0.0f;
		float distance = std::sqrt(offset.x * offset.x + offset.z * offset.z);
		if (distance > arenaRadius_) {
			Vector3 clamped = arenaCenter_ + offset * (arenaRadius_ / distance);
			owner_->GetTransform().translation_.x = clamped.x;
			owner_->GetTransform().translation_.z = clamped.z;
		}
	}

	// 発光もBTの状態に関わらずフェードさせる。**攻撃アクションが今フレーム要求していなければ
	// 待機の明るさへ戻る**ので、攻撃が中断されても光りっぱなしにならない。
	UpdateEmissive(deltaTime);

	// **ビームも同じ扱いで畳む。** 器はランタイム生成した親無しの板なので、
	// スタンや形態移行でBeamRecoverを通らずに攻撃が切れると、空中に置き去りになって残り続ける。
	// 「今フレーム、ビームを使うフェーズを回しているか」だけで決めれば、
	// どの中断経路から抜けても必ず消える。
	if (!IsBeamPhase(currentPhase_)) {
		HideBeam();
	}

	// **倒れたら動かない。** HPが尽きた後もBTが回り続けると、
	// 撃破演出や次の形態への繋ぎの裏で歩き出したり攻撃を出したりする。
	if (health_ && !health_->IsAlive()) {
		// 倒れたらのけぞりも畳む。傾いたまま固まると撃破の絵が壊れる。
		if (GuardianBody* body = GetComponent<GuardianBody>()) {
			body->ClearReactionPose();
		}
		return;
	}

	// **カットシーンが体を預かっている間も動かない。** 置いた位置から勝手に歩き出さないように。
	if (suspended_) {
		return;
	}

	// スタン中: BTを回さず、スタン姿勢だけを作る。
	if (health_ && health_->IsStaggered()) {
		UpdateStunPose(deltaTime);
		return;
	}

	// のけぞり中: 胴体を一瞬沈めて戻すだけ。行動はしない。
	// **致命の仰け反り中はBTを回さない。** 動き出すと演出の絵が壊れる。
	if (criticalRecoilTimer_ > 0.0f) {
		UpdateCriticalRecoil(deltaTime);
		return;
	}

	// **のけぞりでBTを止めない。** リアクションは見た目の加算レイヤーなので、
	// 技を出したまま殴られた反応だけが返る(UpdateHitReactionが毎フレーム書く)。
	BahamutAI::AIContext context{localBlackboard_};
	context.deltaTime = Time::GetDeltaTime();
	context.SetOwner(*owner_);
	context.observer = btObserver_.get();

	btRuntime_.Tick(context);

	// **BTの後に重ねる。** 攻撃が書いた姿勢の上へ加算するので、順番が逆だと消される。
	UpdateHitReaction(deltaTime);
}

// ---------------------------------------------------------------------------
// BT Condition
// ---------------------------------------------------------------------------

bool GuardianBossComponent::IsTargetWithin(const BahamutAI::NodeParams& params) {
	GameObject* target = FindTarget();
	if (!owner_ || !target) {
		return false;
	}

	float distance = params.GetFloat("distance", 8.0f);
	Vector3 toTarget = target->GetTransform().translation_ - owner_->GetTransform().translation_;
	toTarget.y = 0.0f;
	return Length(toTarget) <= distance;
}

// ---------------------------------------------------------------------------
// BT Actions: Movement
// ---------------------------------------------------------------------------

BahamutAI::BTStatus GuardianBossComponent::MoveToTarget(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GameObject* target = FindTarget();
	if (!owner_ || !target) {
		return BahamutAI::BTStatus::Failure;
	}

	float turnSpeed = params.GetFloat("turnSpeed", 2.0f);
	if (turnSpeed > 0.0f) {
		RotateTowardsTarget(turnSpeed, context.deltaTime);
	}

	Vector3 toTarget = target->GetTransform().translation_ - owner_->GetTransform().translation_;
	toTarget.y = 0.0f;
	float distance = Length(toTarget);
	if (distance <= 0.0001f) {
		return BahamutAI::BTStatus::Success;
	}

	// speedが負なら向きはそのままに後退する。距離を取る行動はこれで作る。
	// 第2形態では倍率を掛けて詰めを速くする(後退にも掛かるので、動き全体が機敏になる)。
	float speed = params.GetFloat("speed", 3.0f) * (phase2_ ? phase2SpeedScale_ : 1.0f);
	owner_->GetTransform().translation_ += toTarget * (speed * context.deltaTime / distance);

	// 移動部分のみ。「どこまで近づいたら止まるか」はツリー側でIsTargetWithinと組む。
	return BahamutAI::BTStatus::Success;
}

BahamutAI::BTStatus GuardianBossComponent::FaceTarget(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	if (!owner_ || !FindTarget()) {
		return BahamutAI::BTStatus::Failure;
	}

	if (RotateTowardsTarget(params.GetFloat("turnSpeed", 2.0f), context.deltaTime)) {
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::BodySpin(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GameObject* owner = GetOwner();
	if (!owner) {
		return BahamutAI::BTStatus::Failure;
	}

	// GuardianBodyがrotation_を毎フレーム丸ごと書き直すので、Transformを直接触っても次フレームで消える。
	// 必ずAddSpin経由で足すこと。
	GuardianBody* body = GetComponent<GuardianBody>();
	if (!body) {
		return BahamutAI::BTStatus::Failure;
	}

	float progress = 0.0f;
	bool finished = TickPhase("BodySpin", params.GetFloat("duration", 1.5f), context.deltaTime, progress);

	body->AddSpin(params.GetFloat("speed", 360.0f) * kDegreeToRadian * context.deltaTime);

	return finished ? BahamutAI::BTStatus::Success : BahamutAI::BTStatus::Running;
}

// ---------------------------------------------------------------------------
// BT Actions: 足の攻撃
// ---------------------------------------------------------------------------

BahamutAI::BTStatus GuardianBossComponent::StompRaise(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "StompRaise");
	if (starting) {
		int leg = PickStompLeg(static_cast<int>(params.GetFloat("leg", -1.0f)));
		if (leg < 0) {
			return BahamutAI::BTStatus::Failure;
		}
		activeLeg_ = leg;

		// 今その脚が接地している場所を起点にする。ここから浮かせるので足が飛ばない。
		attackStartLocal_ = ToRootLocal(rig->GetFootWorld(leg));

		// 振り上げ先。定位置の外向き成分を保ったまま、対象側へ寄せて持ち上げる。
		Vector3 home = rig->GetHomeLocal(leg);
		Vector3 forwardBias = {0.0f, 0.0f, params.GetFloat("forward", 0.6f)};
		attackRaisedLocal_ = home + forwardBias;
		attackRaisedLocal_.y += params.GetFloat("height", 2.2f);

		rig->SetCurveTargetLocal(leg, attackStartLocal_);

		// **振り上げた瞬間に予告する。** 着弾点はまだ決まっていないので、
		// 「対象が今いる場所」を暫定の中心として出し、StompSlamで確定値へ差し替える(Amend)。
		// 暫定でも出しておく理由は、受け側が「逃げるか待つか」を決めるのに必要な
		// **命中までの時間**が、この時点でしか稼げないから。
		if (GameObject* target = FindTarget()) {
			float raiseDuration = (std::max)(params.GetFloat("duration", 0.5f), 1.0e-3f) + (std::max)(params.GetFloat("hold", 0.0f), 0.0f);
			float slamDelay = params.GetFloat("slamDelay", 0.2f);

			Vector3 impact = target->GetTransform().translation_;
			impact.y = SampleGroundUnderRoot();

			Threat::Notice notice;
			notice.source = owner_;
			notice.shape = Threat::Shape::Circle;
			notice.element = Threat::Element::Physical;
			notice.origin = impact;
			notice.radius = params.GetFloat("waveRadius", 4.0f);
			notice.hitTime = raiseDuration + slamDelay;
			notice.clearTime = raiseDuration + slamDelay + 0.5f;
			notice.damage = params.GetFloat("waveDamage", 14.0f);
			Threat::Withdraw(attackThreatId_);
			attackThreatId_ = Threat::Announce(notice);
		}
	}

	if (activeLeg_ < 0) {
		return BahamutAI::BTStatus::Failure;
	}

	// **振り上げながら踏む方へ向き直る。**
	// 脚を上げるだけでは「どこへ落ちるか」が読めない。体ごと向きを変えれば、
	// 振り上げの間ずっと狙いが見えているので、落ちる前に横へ抜ける判断ができる。
	RotateTowardsTarget(params.GetFloat("turnSpeed", 2.2f), context.deltaTime);

	float riseDuration = (std::max)(params.GetFloat("duration", 0.5f), 1.0e-3f);
	float hold = (std::max)(params.GetFloat("hold", 0.0f), 0.0f);

	float progress = 0.0f;
	bool finished = TickPhase("StompRaise", riseDuration + hold, context.deltaTime, progress);

	// 振り上げ切ってから hold 秒だけ頂点で静止する。
	// **「ゆっくり上げる」のではなく「速く上げて止める」のが遅延攻撃の見せ方。**
	// 前者は予備動作が間延びするだけだが、後者は振り下ろしのタイミングを外せる。
	float riseProgress = std::clamp(progress * (riseDuration + hold) / riseDuration, 0.0f, 1.0f);

	// weightを0→1へ持っていくと、その脚だけ歩行から曲線制御へ滑らかに移る。
	float eased = SmoothStep(riseProgress);
	rig->SetCurveWeight(activeLeg_, eased);
	rig->SetCurveTargetLocal(activeLeg_, GuardianRigMath::Lerp3(attackStartLocal_, attackRaisedLocal_, eased));

	return finished ? BahamutAI::BTStatus::Success : BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::StompSlam(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig || activeLeg_ < 0) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "StompSlam");
	if (starting) {
		// 着弾点はフェーズ開始時の対象位置で固定する。追尾し続けると避けられなくなるため。
		Vector3 impact = attackRaisedLocal_;
		if (GameObject* target = FindTarget()) {
			Vector3 targetLocal = ToRootLocal(target->GetTransform().translation_);
			targetLocal.y = 0.0f;

			float reach = params.GetFloat("reach", 3.2f);
			float horizontal = std::sqrt(targetLocal.x * targetLocal.x + targetLocal.z * targetLocal.z);
			if (horizontal > reach && horizontal > 0.0001f) {
				targetLocal = targetLocal * (reach / horizontal);
			}
			impact = targetLocal;
		}
		impact.y = params.GetFloat("depth", 0.0f);
		attackImpactLocal_ = impact;

		SetStrikeActive(activeLeg_, true);

		// **着弾点が確定したので予告を差し替える。**
		// ここから先は追尾しないので、受け側は確定した円の外へ出れば必ず避けられる。
		Vector3 impactWorld = GuardianRigMath::ComputeWorldPose(GetOwner()).TransformPoint(attackImpactLocal_);
		impactWorld.y = SampleGroundUnderRoot();

		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Circle;
		notice.element = Threat::Element::Physical;
		notice.origin = impactWorld;
		notice.radius = params.GetFloat("waveRadius", 4.0f);
		notice.hitTime = params.GetFloat("duration", 0.22f);
		notice.clearTime = params.GetFloat("duration", 0.22f) + params.GetFloat("waveDuration", 0.35f);
		notice.damage = params.GetFloat("waveDamage", 14.0f);
		if (attackThreatId_ > 0) {
			Threat::Amend(attackThreatId_, notice);
		} else {
			attackThreatId_ = Threat::Announce(notice);
		}
	}

	float progress = 0.0f;
	bool finished = TickPhase("StompSlam", params.GetFloat("duration", 0.22f), context.deltaTime, progress);

	// 加速しながら落とす。等速だと踏みつけの重さが出ない。
	float eased = EaseInQuad(progress);
	rig->SetCurveWeight(activeLeg_, 1.0f);
	rig->SetCurveTargetLocal(activeLeg_, GuardianRigMath::Lerp3(attackRaisedLocal_, attackImpactLocal_, eased));

	if (finished) {
		SetStrikeActive(activeLeg_, false);
		// 踏み抜いた地点から衝撃刃を広げる。足の判定は先端の球しかなく点でしか当たらないので、
		// 「踏まれた場所の周り」を面で拾うのがこの刃の役目。
		Vector3 impactWorld = GuardianRigMath::ComputeWorldPose(GetOwner()).TransformPoint(attackImpactLocal_);
		impactWorld.y = SampleGroundUnderRoot();
		StartShockwave(impactWorld, params.GetFloat("waveStart", 1.0f), params.GetFloat("waveRadius", 4.0f),
		    params.GetFloat("waveDuration", 0.35f), false, params.GetFloat("waveDamage", 14.0f),
		    params.GetFloat("waveKnockback", 9.0f), params.GetFloat("waveStun", 0.6f), 0.5f);
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::LegSweep(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig || activeLeg_ < 0) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "LegSweep");
	if (starting) {
		SetStrikeActive(activeLeg_, true);

		// 薙ぎ終わりの位置を先に計算して予告する。弧の途中も脚は通るが、
		// 面の判定(衝撃刃)が出るのは薙ぎ終わりなので、そこを危険域として伝える。
		float radius = params.GetFloat("radius", 3.0f);
		float arc = params.GetFloat("arc", 150.0f) * kDegreeToRadian;
		float startAngle = std::atan2(attackRaisedLocal_.x, attackRaisedLocal_.z);
		float endAngle = startAngle + arc * 0.5f;
		Vector3 endLocal = {std::sin(endAngle) * radius, params.GetFloat("height", 0.9f), std::cos(endAngle) * radius};
		Vector3 endWorld = GuardianRigMath::ComputeWorldPose(GetOwner()).TransformPoint(endLocal);
		endWorld.y = SampleGroundUnderRoot();

		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Circle;
		notice.element = Threat::Element::Physical;
		notice.origin = endWorld;
		notice.radius = params.GetFloat("waveRadius", 4.5f);
		notice.hitTime = params.GetFloat("duration", 0.5f);
		notice.clearTime = params.GetFloat("duration", 0.5f) + params.GetFloat("waveDuration", 0.3f);
		notice.damage = params.GetFloat("waveDamage", 12.0f);
		Threat::Withdraw(attackThreatId_);
		attackThreatId_ = Threat::Announce(notice);
	}

	float progress = 0.0f;
	bool finished = TickPhase("LegSweep", params.GetFloat("duration", 0.5f), context.deltaTime, progress);

	// 振り上げ位置の角度を始点として、arc度ぶん水平に薙ぐ。
	float radius = params.GetFloat("radius", 3.0f);
	float arc = params.GetFloat("arc", 150.0f) * kDegreeToRadian;
	float startAngle = std::atan2(attackRaisedLocal_.x, attackRaisedLocal_.z);
	float angle = startAngle - arc * 0.5f + arc * SmoothStep(progress);

	Vector3 sweep = {std::sin(angle) * radius, params.GetFloat("height", 0.9f), std::cos(angle) * radius};
	rig->SetCurveWeight(activeLeg_, 1.0f);
	rig->SetCurveTargetLocal(activeLeg_, sweep);

	if (finished) {
		SetStrikeActive(activeLeg_, false);
		// 薙いだ弧をまとめて拾う衝撃刃。脚の通り道は広いのに判定は先端の球だけなので、
		// 薙ぎ終わりに足元から刃を広げて「薙ぎ払われた」範囲を成立させる。
		Vector3 sweepWorld = GuardianRigMath::ComputeWorldPose(GetOwner()).TransformPoint(sweep);
		sweepWorld.y = SampleGroundUnderRoot();
		StartShockwave(sweepWorld, params.GetFloat("waveStart", 1.5f), params.GetFloat("waveRadius", 4.5f),
		    params.GetFloat("waveDuration", 0.3f), false, params.GetFloat("waveDamage", 12.0f),
		    params.GetFloat("waveKnockback", 11.0f), params.GetFloat("waveStun", 0.6f), 0.5f);
		// 復帰フェーズの起点を薙ぎ終わりに合わせる。
		attackRaisedLocal_ = sweep;
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::StompRecover(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig || activeLeg_ < 0) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "StompRecover");
	if (starting) {
		SetStrikeActive(activeLeg_, false);
		// **ここが後隙。** 脚は投げ出したままで庇えないので、味方AIへ「今が窓だ」と伝える。
		Threat::SetOpen(owner_, params.GetFloat("duration", 0.45f));
	}

	float progress = 0.0f;
	bool finished = TickPhase("StompRecover", params.GetFloat("duration", 0.45f), context.deltaTime, progress);

	// weightを1→0へ戻すだけでよい。0.5を切った時点で歩行側が主導権を取り戻し、
	// 「曲線が運んだ先」を今いる場所として引き継いで踏み直してくれる。
	rig->SetCurveWeight(activeLeg_, 1.0f - SmoothStep(progress));

	if (finished) {
		rig->SetCurveWeight(activeLeg_, 0.0f);
		activeLeg_ = -1;
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

// ---------------------------------------------------------------------------
// BT Actions: 飛びかかり
// ---------------------------------------------------------------------------

BahamutAI::BTStatus GuardianBossComponent::LeapCharge(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	GameObject* target = FindTarget();
	if (!owner_ || !rig || !target) {
		return BahamutAI::BTStatus::Failure;
	}

	RotateTowardsTarget(params.GetFloat("turnSpeed", 3.0f), context.deltaTime);

	if (currentPhase_ != "LeapCharge") {
		// 溜めに入った時点で予告する。着地点は溜めの終わりに確定するので、
		// ここでは対象の現在位置を暫定として出し、LeapFlyで差し替える。
		float duration = params.GetFloat("duration", 0.7f);
		float flyDelay = params.GetFloat("flyDelay", 0.85f);

		Vector3 land = target->GetTransform().translation_;
		land.y = SampleGroundUnderRoot();

		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Circle;
		notice.element = Threat::Element::Physical;
		notice.origin = land;
		notice.radius = params.GetFloat("waveRadius", 6.0f);
		notice.hitTime = duration + flyDelay;
		notice.clearTime = duration + flyDelay + 0.6f;
		notice.damage = params.GetFloat("waveDamage", 18.0f);
		Threat::Withdraw(attackThreatId_);
		attackThreatId_ = Threat::Announce(notice);
	}

	float progress = 0.0f;
	bool finished = TickPhase("LeapCharge", params.GetFloat("duration", 0.7f), context.deltaTime, progress);

	// 沈み込みは脚ではなく胴体を下げて表現する。脚は接地したままなので踏ん張って見える。
	SetBodySink(-params.GetFloat("sink", 0.7f) * SmoothStep(progress));

	if (finished) {
		// 踏み切りと着地点をここで確定する。以降は対象を追わない(避けられる攻撃にするため)。
		leapStartWorld_ = owner_->GetTransform().translation_;
		leapLandWorld_ = target->GetTransform().translation_;
	}
	return finished ? BahamutAI::BTStatus::Success : BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::LeapFly(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "LeapFly");
	if (starting) {
		// 着地点を跳べる距離と手前距離で丸める。
		Vector3 toLand = leapLandWorld_ - leapStartWorld_;
		toLand.y = 0.0f;
		float distance = Length(toLand);

		float stopDistance = params.GetFloat("stopDistance", 2.4f);
		float range = params.GetFloat("range", 16.0f);
		float wanted = std::clamp(distance - stopDistance, 0.0f, range);

		Vector3 direction = GuardianRigMath::SafeNormalize(toLand, {0.0f, 0.0f, 1.0f});
		Vector3 land = leapStartWorld_ + direction * wanted;

		// 着地点の地面高さへ合わせる。歩行と同じレイキャスト設定を使う。
		if (GuardianGait* gait = GetComponent<GuardianGait>()) {
			land.y = gait->SampleGroundAt(land);
		} else {
			land.y = leapStartWorld_.y;
		}
		leapLandWorld_ = land;

		// 4本とも曲線制御へ移す。歩行側はこの間その脚に触らない。
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveTargetLocal(index, ToRootLocal(rig->GetFootWorld(index)));
		}
		SetBodySink(0.0f);

		// **着地点が確定したので予告を差し替える。** 滞空中は追尾しないので、
		// 受け側は0.85秒かけて確定した円の外へ歩いて出られる。
		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Circle;
		notice.element = Threat::Element::Physical;
		notice.origin = leapLandWorld_;
		notice.radius = params.GetFloat("waveRadius", 6.0f);
		notice.hitTime = params.GetFloat("duration", 0.85f);
		notice.clearTime = params.GetFloat("duration", 0.85f) + 0.6f;
		notice.damage = params.GetFloat("waveDamage", 18.0f);
		if (attackThreatId_ > 0) {
			Threat::Amend(attackThreatId_, notice);
		} else {
			attackThreatId_ = Threat::Announce(notice);
		}
	}

	float progress = 0.0f;
	bool finished = TickPhase("LeapFly", params.GetFloat("duration", 0.85f), context.deltaTime, progress);

	// 水平は等速、垂直は放物線。half-sineだと着地が緩むので、正弦の山で素直に上げ下げする。
	Vector3 position = GuardianRigMath::Lerp3(leapStartWorld_, leapLandWorld_, progress);
	position.y += std::sin(std::numbers::pi_v<float> * progress) * params.GetFloat("height", 6.0f);
	owner_->GetTransform().translation_ = position;

	// 脚を畳む。滞空の前半で畳み切って、後半は着地に備えて構えたままにする。
	float tuckBlend = SmoothStep(std::clamp(progress * 2.0f, 0.0f, 1.0f));
	float pull = params.GetFloat("tuck", 0.45f);
	float lift = params.GetFloat("lift", 1.2f);
	for (int index = 0; index < rig->GetLegCount(); ++index) {
		rig->SetCurveWeight(index, 1.0f);
		Vector3 current = rig->GetCurveTargetLocal(index);
		Vector3 tucked = TuckedLocal(index, pull, lift);
		rig->SetCurveTargetLocal(index, GuardianRigMath::Lerp3(current, tucked, tuckBlend * 0.35f));
	}

	if (finished) {
		owner_->GetTransform().translation_ = leapLandWorld_;
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::LeapSlam(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "LeapSlam");
	if (starting) {
		SetAllStrikesActive(true);
	}

	float progress = 0.0f;
	bool finished = TickPhase("LeapSlam", params.GetFloat("duration", 0.18f), context.deltaTime, progress);

	// 畳んだ位置から一気に外へ叩きつける。加速させて重さを出す。
	float eased = EaseInQuad(progress);
	float spread = params.GetFloat("spread", 3.6f);
	float depth = params.GetFloat("depth", -0.1f);
	for (int index = 0; index < rig->GetLegCount(); ++index) {
		Vector3 from = TuckedLocal(index, params.GetFloat("tuck", 0.45f), params.GetFloat("lift", 1.2f));
		Vector3 to = SpreadLocal(index, spread, depth);
		rig->SetCurveWeight(index, 1.0f);
		rig->SetCurveTargetLocal(index, GuardianRigMath::Lerp3(from, to, eased));
	}

	if (finished) {
		SetAllStrikesActive(false);
		// 着地の踏み潰しは全周へ。4本の脚の間をすり抜けられないようにする。
		Vector3 landWorld = owner_->GetTransform().translation_;
		landWorld.y = SampleGroundUnderRoot();
		StartShockwave(landWorld, params.GetFloat("waveStart", 1.5f), params.GetFloat("waveRadius", 6.0f),
		    params.GetFloat("waveDuration", 0.4f), false, params.GetFloat("waveDamage", 18.0f),
		    params.GetFloat("waveKnockback", 12.0f), params.GetFloat("waveStun", 0.8f), 0.5f);
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::LeapRecover(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "LeapRecover");
	if (starting) {
		SetAllStrikesActive(false);
		SetBodySink(0.0f);
		// 着地の踏み潰しから脚を戻す間が後隙。
		Threat::SetOpen(owner_, params.GetFloat("duration", 0.5f));
	}

	float progress = 0.0f;
	bool finished = TickPhase("LeapRecover", params.GetFloat("duration", 0.5f), context.deltaTime, progress);

	// weightを戻すだけで、歩行側が「曲線が運んだ先」を引き継いで踏み直す。
	float weight = 1.0f - SmoothStep(progress);
	for (int index = 0; index < rig->GetLegCount(); ++index) {
		rig->SetCurveWeight(index, weight);
	}

	if (finished) {
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveWeight(index, 0.0f);
		}
		activeLeg_ = -1;
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

// ---------------------------------------------------------------------------
// BT Actions: ジェット飛行
// ---------------------------------------------------------------------------

BahamutAI::BTStatus GuardianBossComponent::JetWarn(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "JetWarn");
	if (starting) {
		// 予兆中は当たらない。「これから来る」とだけ伝える時間にする。
		SetAllStrikesActive(false);
		SetAllStrikeScales(1.0f);

		// **持続する追従型として予告する。** 刃は1秒間ボスに付いて回るので、
		// 無敵0.45秒では覆えない。sustained=true が「転がるな、離脱しろ」の合図になる。
		float duration = params.GetFloat("duration", 1.2f);
		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Circle;
		notice.element = Threat::Element::Physical;
		notice.origin = owner_->GetTransform().translation_;
		notice.radius = params.GetFloat("waveRadius", 4.5f);
		notice.followSource = true;
		notice.sustained = true;
		notice.hitTime = duration;
		notice.clearTime = duration + params.GetFloat("chaseDuration", 2.2f);
		notice.damage = params.GetFloat("waveDamage", 16.0f);
		Threat::Withdraw(attackThreatId_);
		attackThreatId_ = Threat::Announce(notice);
	}

	float progress = 0.0f;
	bool finished = TickPhase("JetWarn", params.GetFloat("duration", 1.2f), context.deltaTime, progress);

	float eased = SmoothStep(progress);

	// 沈み込んで脚を抱え込む(バネを縮めるように見せる)。
	SetBodySink(-params.GetFloat("sink", 0.6f) * eased);
	float tuck = params.GetFloat("tuck", 0.35f);
	float lift = params.GetFloat("lift", 0.5f);
	for (int index = 0; index < rig->GetLegCount(); ++index) {
		rig->SetCurveStraight(index, false);
		// weightを0から上げるので、歩行姿勢から抱え込み姿勢へ自然に混ざる。
		rig->SetCurveWeight(index, eased);
		rig->SetCurveTargetLocal(index, TuckedLocal(index, tuck, lift));
	}

	// ゆっくり回り始める(本番の高速回転への助走)。
	SpinRootYaw(params.GetFloat("spinSpeed", 240.0f) * eased, context.deltaTime);

	// 胴体を赤く点滅させる。終盤ほど速く見えるよう、明滅は経過時間で回す。
	float flashHz = params.GetFloat("flashHz", 6.0f);
	float pulse = 0.5f + 0.5f * std::sin(phaseTimer_ * flashHz * 2.0f * std::numbers::pi_v<float>);
	SetBodyEmissive(true, Vector3{1.0f, 0.15f, 0.05f}, params.GetFloat("flashIntensity", 8.0f) * pulse);

	if (finished) {
		// 発光は予兆の役目が終わった時点で消す(攻撃本体へ持ち込まない)。
		// 第2形態では常時光っているので、消すのではなく基準へ戻す。
		RestoreBaseEmissive();
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::JetLiftOff(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "JetLiftOff");
	if (starting) {
		jetGroundY_ = SampleGroundUnderRoot();
		// 今その脚がある場所から回転姿勢へ移すので、いきなり飛ばない。
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveTargetLocal(index, ToRootLocal(rig->GetFootWorld(index)));
		}
	}

	float progress = 0.0f;
	bool finished = TickPhase("JetLiftOff", params.GetFloat("duration", 0.6f), context.deltaTime, progress);

	float eased = SmoothStep(progress);
	float height = params.GetFloat("height", -0.7f) * eased;
	owner_->GetTransform().translation_.y = jetGroundY_ + height;

	// 胴体ごと回す。離陸中は weight と一緒に徐々に効かせる。
	SpinRootYaw(params.GetFloat("spinSpeed", 540.0f) * eased, context.deltaTime);

	for (int index = 0; index < rig->GetLegCount(); ++index) {
		rig->SetCurveWeight(index, eased);
	}
	ApplyLegBlades(params.GetFloat("spinReach", 1.0f), params.GetFloat("spinPitch", 0.0f), true);

	return finished ? BahamutAI::BTStatus::Success : BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::JetChase(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "JetChase");
	// 回転する脚がそのまま刃になる。**毎Tick入れ直す** —
	// 開始時に一度だけONにする作りだと、途中で何かがOFFにしたときに気づけないため。
	SetAllStrikesActive(true);
	// 刃の先端は1フレームに1m近く進むので、判定を太らせないとプレイヤーをすり抜ける。
	SetAllStrikeScales(params.GetFloat("strikeScale", 2.0f));

	if (starting) {
		// **脚の先端だけでは当たらない。** 刃は接合部から外へ伸びているので、
		// 密着されるとプレイヤーが回転の輪の内側に入り、先端の球は永久にすり抜ける。
		// 掃いている円盤そのものを判定にするため、ボスに追従する一定サイズの刃を出す。
		Vector3 center = owner_->GetTransform().translation_;
		center.y = SampleGroundUnderRoot() + params.GetFloat("waveHeight", 0.8f);
		float bladeRadius = params.GetFloat("waveRadius", 4.5f);
		// duration<=0 = JetChaseが終わるまで出しっぱなし。
		StartShockwave(center, bladeRadius, bladeRadius, 0.0f, true, params.GetFloat("waveDamage", 16.0f),
		    params.GetFloat("waveKnockback", 12.0f), params.GetFloat("waveStun", 0.7f), params.GetFloat("waveHitInterval", 0.7f));
	}

	float progress = 0.0f;
	bool finished = TickPhase("JetChase", params.GetFloat("duration", 5.0f), context.deltaTime, progress);

	float height = params.GetFloat("height", -0.7f);

	// 追跡。**旋回はしない** — Yawはコマの回転が使っているので、向きを変えずに位置だけ寄せる。
	// 接地していないので歩幅の制約が無く、歩行より速くできる。
	if (GameObject* target = FindTarget()) {

		Vector3 toTarget = target->GetTransform().translation_ - owner_->GetTransform().translation_;
		toTarget.y = 0.0f;
		float distance = Length(toTarget);
		if (distance > 0.0001f) {
			owner_->GetTransform().translation_ += toTarget * (params.GetFloat("speed", 2.5f) * context.deltaTime / distance);
		}
	}

	// 移動先の地面へ高度を合わせ直す。起伏があってもホバー高度を保つ。
	jetGroundY_ = SampleGroundUnderRoot();
	owner_->GetTransform().translation_.y = jetGroundY_ + height;

	SpinRootYaw(params.GetFloat("spinSpeed", 900.0f), context.deltaTime);
	for (int index = 0; index < rig->GetLegCount(); ++index) {
		rig->SetCurveWeight(index, 1.0f);
	}
	ApplyLegBlades(params.GetFloat("spinReach", 1.0f), params.GetFloat("spinPitch", 0.0f), true);

	if (finished) {
		SetAllStrikesActive(false);
		SetAllStrikeScales(1.0f);
		StopShockwave();
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::JetLand(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "JetLand");
	if (starting) {
		SetAllStrikesActive(false);
		// 開始時の高度を固定する。毎フレーム現在高度から取り直すと、
		// 減っていく高さに毎回eased率を掛けることになり指数的に落ちてしまう。
		jetGroundY_ = SampleGroundUnderRoot();
		jetLandStartHeight_ = owner_->GetTransform().translation_.y - jetGroundY_;
	}

	float progress = 0.0f;
	bool finished = TickPhase("JetLand", params.GetFloat("duration", 0.6f), context.deltaTime, progress);

	float eased = SmoothStep(progress);

	// 高度を0へ落としつつ、weightも0へ戻す。
	// weightが0.5を切った時点で歩行側が主導権を取り戻し、その場から踏み直す。
	jetGroundY_ = SampleGroundUnderRoot();
	float landedHeight = jetLandStartHeight_ * (1.0f - eased);
	owner_->GetTransform().translation_.y = jetGroundY_ + landedHeight;

	SpinRootYaw(params.GetFloat("spinSpeed", 360.0f) * (1.0f - eased), context.deltaTime);
	for (int index = 0; index < rig->GetLegCount(); ++index) {
		rig->SetCurveWeight(index, 1.0f - eased);
	}
	ApplyLegBlades(params.GetFloat("spinReach", 1.0f), params.GetFloat("spinPitch", 0.0f), true);

	if (finished) {
		owner_->GetTransform().translation_.y = jetGroundY_;
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveWeight(index, 0.0f);
			// **直線モードは必ずここで解除する。**
			// 立てっぱなしにすると歩行も他の攻撃も棒のままになる
			// (curveStraight_ は curveWeight と無関係に効くため)。
			rig->SetCurveStraight(index, false);
		}
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// BT Actions: Beam
//
// 脚を一切触らないので、歩行(GuardianGait)と完全に独立している。
// 「攻撃 = 何かの姿を時間で動かす」という骨格は踏みつけと同じで、
// 動かす対象が脚の曲線ではなく1個の子オブジェクトになっただけ。
// ---------------------------------------------------------------------------

BahamutAI::BTStatus GuardianBossComponent::BeamCharge(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	// 子オブジェクトが無い個体では、ここでPrefabから器を作る。
	GameObject* beam = AcquireBeam();
	if (!owner_ || !beam) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "BeamCharge");
	if (starting) {
		beam->SetActive(true);
		// 溜め中は当てない。ここは「来るぞ」と伝えるための時間で、当たると回避の意味が消える。
		SetBeamAttack(false);

		// **線として予告する。** 円と違って「外へ逃げる」ではなく「横へ抜ける」が正解になるので、
		// 形を伝えることに意味がある。照射は0.5〜1.0秒続くので持続型(sustained)。
		float duration = params.GetFloat("duration", 1.2f);
		Vector3 origin = owner_->GetTransform().translation_;
		origin.y = SampleGroundUnderRoot();

		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Line;
		notice.element = Threat::Element::Magic;
		notice.origin = origin;
		notice.direction = {std::sin(GetRootYaw()), 0.0f, std::cos(GetRootYaw())};
		notice.radius = params.GetFloat("thickness", 0.35f) * 2.0f;
		notice.length = params.GetFloat("length", 14.0f);
		notice.sustained = true;
		notice.hitTime = duration;
		notice.clearTime = duration + params.GetFloat("fireDuration", 1.0f);
		notice.damage = 12.0f;
		Threat::Withdraw(attackThreatId_);
		attackThreatId_ = Threat::Announce(notice);
	}

	// 溜めの間に正面を取り切る。ここで取り切っておくから、照射中はゆっくり追ってよくなる。
	RotateTowardsTarget(params.GetFloat("turnSpeed", 2.5f), context.deltaTime);

	float progress = 0.0f;
	bool finished = TickPhase("BeamCharge", params.GetFloat("duration", 1.2f), context.deltaTime, progress);

	// **点滅で予告する。** だんだん太くするより、点滅→実射のほうが「来る」瞬間がはっきりして迫力が出る。
	// 太さは照射時と同じにして、違いを「点滅しているか / 実体か」だけに絞るのがコツ。
	float blinkInterval = (std::max)(params.GetFloat("blinkInterval", 0.12f), 1.0e-3f);
	beam->SetActive((static_cast<int>(phaseTimer_ / blinkInterval) % 2) == 0);
	float length = params.GetFloat("length", 14.0f);
	float thickness = params.GetFloat("thickness", 0.35f);
	UpdateBeam(length, thickness, params.GetFloat("aimHeight", 1.0f));

	// 溜め中は**速く**旋回するので、向きは毎フレーム差し替える。
	// 出しっぱなしの古い線を信じさせると、狙いを付け直された先で焼かれる。
	{
		float yaw = GetRootYaw();
		Vector3 origin = owner_->GetTransform().translation_;
		origin.y = SampleGroundUnderRoot();

		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Line;
		notice.element = Threat::Element::Magic;
		notice.origin = origin;
		notice.direction = {std::sin(yaw), 0.0f, std::cos(yaw)};
		notice.radius = thickness * 2.0f;
		notice.length = length;
		notice.sustained = true;
		notice.hitTime = (std::max)(params.GetFloat("duration", 1.2f) - phaseTimer_, 0.0f);
		notice.clearTime = notice.hitTime + params.GetFloat("fireDuration", 1.0f);
		notice.damage = 12.0f;
		if (attackThreatId_ > 0) {
			Threat::Amend(attackThreatId_, notice);
		} else {
			attackThreatId_ = Threat::Announce(notice);
		}
	}

	return finished ? BahamutAI::BTStatus::Success : BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::BeamFire(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GameObject* beam = AcquireBeam();
	if (!owner_ || !beam) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "BeamFire");
	if (starting) {
		beam->SetActive(true);
		SetBeamAttack(true);
	}

	// **照射中はわざと遅く旋回する。** 速いと必ず当たる=避けようがない攻撃になる。
	// 「走り続ければ抜けられるが、止まれば焼かれる」の境目がこの数値。
	RotateTowardsTarget(params.GetFloat("turnSpeed", 0.8f), context.deltaTime);

	float progress = 0.0f;
	bool finished = TickPhase("BeamFire", params.GetFloat("duration", 1.0f), context.deltaTime, progress);

	float length = params.GetFloat("length", 14.0f);
	float thickness = params.GetFloat("thickness", 0.35f);
	UpdateBeam(length, thickness, params.GetFloat("aimHeight", 1.0f));

	// **予告の向きを毎フレーム差し替える。**
	// 照射中もゆっくり旋回するので、溜め開始時の向きのまま伝えると、
	// 受け側は「昔そこにあった線」から避けることになり、実際の帯には入ったままになる
	// (回転ビームでは直していたのに、こちらは追従させ忘れていた)。
	{
		float yaw = GetRootYaw();
		Vector3 origin = owner_->GetTransform().translation_;
		origin.y = SampleGroundUnderRoot();

		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Line;
		notice.element = Threat::Element::Magic;
		notice.origin = origin;
		notice.direction = {std::sin(yaw), 0.0f, std::cos(yaw)};
		notice.radius = thickness * 2.0f;
		notice.length = length;
		notice.sustained = true;
		notice.hitTime = 0.0f;
		notice.clearTime = (std::max)(params.GetFloat("duration", 1.0f) - phaseTimer_, 0.05f);
		notice.damage = 12.0f;
		if (attackThreatId_ > 0) {
			Threat::Amend(attackThreatId_, notice);
		} else {
			attackThreatId_ = Threat::Announce(notice);
		}
	}

	if (finished) {
		SetBeamAttack(false);
		Threat::Withdraw(attackThreatId_);
		attackThreatId_ = 0;
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::BeamRecover(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GameObject* beam = AcquireBeam();
	if (!owner_ || !beam) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "BeamRecover");
	if (starting) {
		SetBeamAttack(false);
		// 照射を切ってから消えるまでが後隙。
		Threat::SetOpen(owner_, params.GetFloat("duration", 0.15f));
	}

	float progress = 0.0f;
	bool finished = TickPhase("BeamRecover", params.GetFloat("duration", 0.15f), context.deltaTime, progress);

	// 細めてから消す。判定はもう切れているので、ここは「終わった」と見せるためだけの時間。
	float thickness = std::lerp(params.GetFloat("thickness", 0.35f), 0.0f, SmoothStep(progress));
	UpdateBeam(params.GetFloat("length", 14.0f), thickness, params.GetFloat("aimHeight", 1.0f));

	if (finished) {
		HideBeam();
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

KujataEngine::GameObject* GuardianBossComponent::GetBeamObject() {
	if (!owner_) {
		return nullptr;
	}
	if (GameObject* child = FindDescendantByName(owner_, beamObjectName_)) {
		return child;
	}
	// ランタイム生成した器(親を持たないワールド空間の板)。
	return beam_;
}

KujataEngine::GameObject* GuardianBossComponent::AcquireBeam() {
	if (GameObject* existing = GetBeamObject()) {
		return existing;
	}
	if (beamTried_ || !owner_ || !owner_->GetScene() || beamPrefabPath_.empty()) {
		return nullptr;
	}

	// **一度だけ試す。** 失敗するたびに毎フレーム読み直すと、Update中に生成を繰り返して重くなる。
	beamTried_ = true;
	PrefabAsset::InstantiateResult result = PrefabAsset::Instantiate(*owner_->GetScene(), beamPrefabPath_, false);
	if (!result.succeeded || !result.rootObject) {
		Logger::Log("[GuardianBoss] beam prefab load failed (" + beamPrefabPath_ + "): " + result.message);
		return nullptr;
	}
	beam_ = result.rootObject;
	beam_->SetActive(false);
	// 体と同じレイヤーへ置いてカメラの障害物判定から外す(脚と同じ理由)。
	beam_->SetLayer(static_cast<uint32_t>(std::clamp(bodyPartLayer_, 0, 31)));
	return beam_;
}

KujataEngine::Vector3 GuardianBossComponent::GetEyeWorldPosition() const {
	if (GuardianBody* body = GetComponent<GuardianBody>()) {
		if (GameObject* eye = body->GetEyeObject()) {
			return GuardianRigMath::ComputeWorldPose(eye).position;
		}
	}
	if (!owner_) {
		return {0.0f, 0.0f, 0.0f};
	}
	return owner_->GetTransform().translation_ + Vector3{0.0f, beamHeight_, 0.0f};
}

float GuardianBossComponent::GetRootYaw() const { return owner_ ? owner_->GetTransform().rotation_.y : 0.0f; }

float GuardianBossComponent::YawToTarget() const {
	GameObject* target = const_cast<GuardianBossComponent*>(this)->FindTarget();
	if (!owner_ || !target) {
		return GetRootYaw();
	}
	Vector3 diff = target->GetTransform().translation_ - owner_->GetTransform().translation_;
	return std::atan2(diff.x, diff.z);
}

/// <summary>
/// **目の居場所から対象を見たときのYaw。** 第2形態の目は脚から遠く離れているので、
/// 脚基準のYawToTargetを流用すると、目から明後日の方向へビームが伸びる。
/// 目が脚に付いている構成(第1形態)では YawToTarget と同じ値になる。
/// </summary>
float GuardianBossComponent::YawFromEyeToTarget() const {
	GameObject* target = const_cast<GuardianBossComponent*>(this)->FindTarget();
	if (!target) {
		return GetRootYaw();
	}
	Vector3 origin = GetEyeWorldPosition();
	Vector3 toTarget = target->GetTransform().translation_ - origin;
	toTarget.y = 0.0f;
	if (Length(toTarget) < 0.0001f) {
		return GetRootYaw();
	}
	return std::atan2(toTarget.x, toTarget.z);
}

void GuardianBossComponent::UpdateEyeBeam(float yaw, float pitch, float length, float thickness) {
	GameObject* beam = AcquireBeam();
	if (!beam) {
		return;
	}

	// 進行方向。+Zを前とする左手系では、X軸まわりの正の回転が下を向く。
	Vector3 forward = {std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};

	// Cubeは原点が中心なので、長さLにするには scale.z=L と「向いた方へ半分だけ出す」がセットで要る。
	Vector3 origin = GetEyeWorldPosition();

	// **地面に着くまで伸ばす。** 目が高いところにいると、パラメータの長さで切ったビームは
	// 空中で途切れて「どこを焼いているのか」が読めない。地面との交点まで伸ばすと、
	// 焼けている場所が床に出るので、避ける先が目で分かる。
	// 下を向いていない(pitchがほぼ0以下)ときは伸ばしようがないので、指定の長さのまま。
	float aboveGround = origin.y - SampleGroundUnderRoot();
	float sinPitch = std::sin(pitch);
	if (aboveGround > 0.05f && sinPitch > 0.02f) {
		length = (std::min)((std::max)(aboveGround / sinPitch, length), beamMaxLength_);
	}
	WorldTransform& transform = beam->GetTransform();
	transform.translation_ = origin + forward * (length * 0.5f);
	transform.rotation_ = {pitch, yaw, 0.0f};
	transform.scale_ = {thickness, thickness, length};
}

// ---------------------------------------------------------------------------
// 発光
//
// **通常時は低く、攻撃中だけ強く。** 予兆の点滅(SetBodyEmissive)とは別系統で、
// こちらは「動き出したか」を伝える地の明るさを担当する。
// 第2形態では色が赤へ変わる(強さではなく**色**で形態を示すのが一番読み取りやすい)。
// ---------------------------------------------------------------------------

void GuardianBossComponent::RequestEmissive(float intensity) { requestedEmissive_ = (std::max)(requestedEmissive_, intensity); }

void GuardianBossComponent::UpdateEmissive(float deltaTime) {
	// 攻撃フェーズを回している間は明るい方を要求する(TickPhaseが立てるタイマーで判定)。
	if (attackGlowTimer_ > 0.0f) {
		attackGlowTimer_ -= deltaTime;
		RequestEmissive(activeEmissiveIntensity_);
	}

	float target = (requestedEmissive_ >= 0.0f) ? requestedEmissive_ : idleEmissiveIntensity_;
	if (phase2_) {
		// 第2形態は常に一段明るい。色が赤なので、同じ強さでも十分に違って見える。
		target = (std::max)(target, phase2EmissiveIntensity_);
	}
	requestedEmissive_ = -1.0f;

	float rate = std::clamp(emissiveFadeRate_ * deltaTime, 0.0f, 1.0f);
	currentEmissive_ += (target - currentEmissive_) * rate;

	SetBodyEmissive(true, phase2_ ? phase2EmissiveColor_ : baseEmissiveColor_, currentEmissive_);
}

// ---------------------------------------------------------------------------
// 弾(光球/胞子)のプール
//
// 敵側の弾は「EnemyWeapon を持つ器を、コードで動かす」だけで作れる。
// 味方の MagicProjectile を流用しないのは、あちらが EnemyHealth を殴る側の道具だから。
// 器はPrefabから作って**使い回す**(Update中に生成と破棄を繰り返さない)。
// ---------------------------------------------------------------------------

void GuardianBossComponent::SpawnOrb(const Vector3& position, const Vector3& velocity, float radius, float lifetime, bool linger,
    float damage, GameObject* homingTarget, float homingTurnRate) {
	if (!owner_ || !owner_->GetScene() || orbPrefabPath_.empty() || orbPrefabFailed_) {
		return;
	}

	// 空いている器を探す。無ければ1つ作る。
	Orb* slot = nullptr;
	for (Orb& orb : orbs_) {
		if (!orb.active) {
			slot = &orb;
			break;
		}
	}
	if (!slot) {
		PrefabAsset::InstantiateResult result = PrefabAsset::Instantiate(*owner_->GetScene(), orbPrefabPath_, false);
		if (!result.succeeded || !result.rootObject) {
			// **一度失敗したら諦める。** 毎フレーム読み直すとUpdate中に生成を繰り返して重くなる。
			orbPrefabFailed_ = true;
			Logger::Log("[GuardianBoss] orb prefab load failed (" + orbPrefabPath_ + "): " + result.message);
			return;
		}
		result.rootObject->SetLayer(static_cast<uint32_t>(std::clamp(bodyPartLayer_, 0, 31)));
		orbs_.push_back(Orb{});
		slot = &orbs_.back();
		slot->object = result.rootObject;
	}

	slot->active = true;
	slot->landed = false;
	slot->homingTarget = homingTarget;
	slot->homingTurnRate = homingTurnRate;
	slot->linger = linger;
	slot->lifetime = lifetime;
	slot->velocity = velocity;

	WorldTransform& transform = slot->object->GetTransform();
	transform.translation_ = position;
	transform.scale_ = {radius, radius, radius};
	slot->object->SetActive(true);

	if (EnemyWeapon* weapon = slot->object->GetComponent<EnemyWeapon>()) {
		weapon->SetHitParams(damage, 6.0f, 0.4f, 0.6f);
		weapon->SetAttack(true);
	}
}

void GuardianBossComponent::UpdateOrbs(float deltaTime) {
	if (orbs_.empty()) {
		return;
	}
	float groundY = SampleGroundUnderRoot();

	for (Orb& orb : orbs_) {
		if (!orb.active || !orb.object) {
			continue;
		}

		orb.lifetime -= deltaTime;
		if (orb.lifetime <= 0.0f) {
			orb.active = false;
			orb.object->SetActive(false);
			continue;
		}

		if (!orb.landed) {
			WorldTransform& transform = orb.object->GetTransform();

			// **追尾弾は速さを保ったまま向きだけ曲げる。**
			// 速度そのものを目標へ向け直すと、近づくほど鋭角に曲がって当たり確定の弾になる。
			// 曲がれる角度を1秒あたりで頭打ちにしておけば、走り続ければ振り切れる。
			if (orb.homingTarget && orb.homingTurnRate > 0.0f && orb.homingTarget->IsActiveInHierarchy()) {
				Vector3 toTarget = orb.homingTarget->GetTransform().translation_;
				toTarget.y += 1.0f;
				toTarget = toTarget - transform.translation_;
				float distance = Length(toTarget);
				float speed = Length(orb.velocity);
				if (distance > 0.0001f && speed > 0.0001f) {
					Vector3 desired = toTarget * (speed / distance);
					float blend = std::clamp(orb.homingTurnRate * deltaTime, 0.0f, 1.0f);
					Vector3 mixed = GuardianRigMath::Lerp3(orb.velocity, desired, blend);
					float mixedSpeed = Length(mixed);
					if (mixedSpeed > 0.0001f) {
						orb.velocity = mixed * (speed / mixedSpeed);
					}
				}
			}

			transform.translation_ += orb.velocity * deltaTime;
			// 落ちる弾は重力で加速する。放物線になるので、どこへ落ちるかが目で追える。
			// **追尾弾だけは落とさない。** 曲がりながら落ちると、狙いと軌道が二重にぶれて読めなくなる。
			if (!orb.homingTarget) {
				orb.velocity.y -= 18.0f * deltaTime;
			}

			if (transform.translation_.y <= groundY + transform.scale_.y * 0.5f) {
				transform.translation_.y = groundY + transform.scale_.y * 0.5f;
				orb.landed = true;
				GameFx::Burst(owner_->GetScene(), GameFx::Prefab::kDust, transform.translation_, 0.8f);
				if (!orb.linger) {
					// 着弾して消える弾。消える前に一瞬だけ判定を残したいので短い寿命を与える。
					orb.lifetime = (std::min)(orb.lifetime, 0.12f);
				}
			}
		}
	}
}

void GuardianBossComponent::ClearOrbs() {
	for (Orb& orb : orbs_) {
		if (orb.object) {
			if (EnemyWeapon* weapon = orb.object->GetComponent<EnemyWeapon>()) {
				weapon->SetAttack(false);
			}
			orb.object->SetActive(false);
		}
		orb.active = false;
	}
}

// ---------------------------------------------------------------------------
// BT Actions: 目(Eye)の攻撃
//
// 目は土台(接合部をぶら下げる器)から完全に分離しているので、ここでの操作は
// 歩行にも脚の攻撃にも一切影響しない。「目だけで戦う攻撃」を足すときはこの並びに書く。
// ---------------------------------------------------------------------------

BahamutAI::BTStatus GuardianBossComponent::EyeAimUp(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GuardianBody* body = GetComponent<GuardianBody>();
	if (!owner_ || !body) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "EyeAimUp");
	if (starting) {
		SetBeamAttack(false); // 予兆では当てない。ここは「来るぞ」と伝えるための時間。
	}

	// 溜めの間に正面を取り切る。ここで取り切っておくから、振り下ろしは対象を追わなくてよくなる。
	RotateTowardsTarget(params.GetFloat("turnSpeed", 2.5f), context.deltaTime);
	eyeAimYaw_ = YawFromEyeToTarget();

	float progress = 0.0f;
	bool finished = TickPhase("EyeAimUp", params.GetFloat("duration", 1.2f), context.deltaTime, progress);

	// 真上(-90度)へ向ける。ピッチは下向きが正なので、真上は負。
	float upPitch = -std::numbers::pi_v<float> * 0.5f;
	body->SetEyePitch(upPitch);
	UpdateEyeBeam(eyeAimYaw_, upPitch, params.GetFloat("length", 14.0f), params.GetFloat("thickness", 0.35f));

	// **点滅で予告する。** 太さを変えず「点滅しているか/実体か」だけの違いにすると
	// 実射の瞬間がはっきりする(BeamChargeと同じ作法)。
	float blinkInterval = (std::max)(params.GetFloat("blinkInterval", 0.12f), 1.0e-3f);
	if (GameObject* beam = GetBeamObject()) {
		beam->SetActive((static_cast<int>(phaseTimer_ / blinkInterval) % 2) == 0);
	}
	RequestEmissive(activeEmissiveIntensity_);

	return finished ? BahamutAI::BTStatus::Success : BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::EyeBeamSlam(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GuardianBody* body = GetComponent<GuardianBody>();
	if (!owner_ || !body) {
		return BahamutAI::BTStatus::Failure;
	}

	float duration = (std::max)(params.GetFloat("duration", 0.5f), 1.0e-3f);
	float waveRadius = params.GetFloat("waveRadius", 4.5f);

	bool starting = (currentPhase_ != "EyeBeamSlam");
	if (starting) {
		if (GameObject* beam = GetBeamObject()) {
			beam->SetActive(true);
		}
		SetBeamAttack(true);

		// **着弾点を今ここで固定して予告する。** 追い続けると避けようがない攻撃になる。
		Vector3 impact = owner_->GetTransform().translation_;
		if (GameObject* target = FindTarget()) {
			impact = target->GetTransform().translation_;
		}
		impact.y = SampleGroundUnderRoot();
		eyeAimYaw_ = std::atan2(impact.x - owner_->GetTransform().translation_.x, impact.z - owner_->GetTransform().translation_.z);

		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Circle;
		notice.element = Threat::Element::Magic;
		notice.origin = impact;
		notice.radius = waveRadius;
		notice.hitTime = duration;
		notice.clearTime = duration + 0.5f;
		notice.damage = params.GetFloat("waveDamage", 14.0f);
		eyeThreatId_ = Threat::Announce(notice);
	}

	float progress = 0.0f;
	bool finished = TickPhase("EyeBeamSlam", duration, context.deltaTime, progress);

	// 真上から地面へ。加速して落とす(踏みつけと同じで、等速だと重さが出ない)。
	float upPitch = -std::numbers::pi_v<float> * 0.5f;
	float downPitch = std::numbers::pi_v<float> * 0.5f;
	float pitch = std::lerp(upPitch, downPitch, EaseInQuad(progress));
	body->SetEyePitch(pitch);
	UpdateEyeBeam(eyeAimYaw_, pitch, params.GetFloat("length", 14.0f), params.GetFloat("thickness", 0.35f));
	RequestEmissive(activeEmissiveIntensity_);

	if (finished) {
		SetBeamAttack(false);
		Vector3 impact = owner_->GetTransform().translation_;
		if (GameObject* target = FindTarget()) {
			impact = target->GetTransform().translation_;
		}
		impact.y = SampleGroundUnderRoot();
		StartShockwave(impact, 1.0f, waveRadius, 0.35f, false, params.GetFloat("waveDamage", 14.0f),
		    params.GetFloat("waveKnockback", 10.0f), params.GetFloat("waveStun", 0.6f), 0.5f);
		Threat::Withdraw(eyeThreatId_);
		eyeThreatId_ = 0;
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::EyeBeamSpin(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GuardianBody* body = GetComponent<GuardianBody>();
	if (!owner_ || !body) {
		return BahamutAI::BTStatus::Failure;
	}

	float duration = (std::max)(params.GetFloat("duration", 2.07f), 1.0e-3f);
	float length = params.GetFloat("length", 14.0f);
	float thickness = params.GetFloat("thickness", 0.35f);
	float pitch = params.GetFloat("pitch", 6.0f) * kDegreeToRadian;

	bool starting = (currentPhase_ != "EyeBeamSpin");
	if (starting) {
		if (GameObject* beam = GetBeamObject()) {
			beam->SetActive(true);
		}
		SetBeamAttack(true);
		eyeAimYaw_ = YawFromEyeToTarget();
		eyeThreatId_ = 0;
	}

	float progress = 0.0f;
	bool finished = TickPhase("EyeBeamSpin", duration, context.deltaTime, progress);

	// 目だけを回す。**ルートは回さない** — 脚は接地したままで、回っているのは目とビームだけ。
	eyeAimYaw_ += params.GetFloat("spinSpeed", 200.0f) * kDegreeToRadian * context.deltaTime;
	// 独立オブジェクトの目は回転がそのままワールド。親子のままなら脚の旋回ぶんを引く。
	body->SetEyeYaw(body->IsEyeDetachedObject() ? eyeAimYaw_ : eyeAimYaw_ - GetRootYaw());
	body->SetEyePitch(pitch);
	UpdateEyeBeam(eyeAimYaw_, pitch, length, thickness);
	RequestEmissive(activeEmissiveIntensity_);

	// **「線」として毎フレーム向きごと差し替える。**
	// 全周を覆う円として予告すると、受け側は半径14mの外まで逃げるしかなくなり、
	// 実際には細い帯が通り過ぎるだけの攻撃に対して戦線を丸ごと捨てることになる。
	// 線で伝えれば「帯の外へ半歩ずれる」という正しい対処が選べる。
	Threat::Notice notice;
	notice.source = owner_;
	notice.shape = Threat::Shape::Line;
	notice.element = Threat::Element::Magic;
	// 帯の起点はビームの根元、つまり**目**。脚の位置で伝えると、目が離れているぶん
	// 実際に薙いでいる場所とずれた線を味方AIに渡すことになる。
	notice.origin = GetEyeWorldPosition();
	notice.origin.y = SampleGroundUnderRoot();
	notice.direction = {std::sin(eyeAimYaw_), 0.0f, std::cos(eyeAimYaw_)};
	notice.radius = thickness * 2.0f;
	notice.length = length;
	notice.sustained = true;
	notice.hitTime = 0.0f;
	notice.clearTime = (std::max)(duration - phaseTimer_, 0.05f);
	notice.damage = 12.0f;
	if (eyeThreatId_ > 0) {
		Threat::Amend(eyeThreatId_, notice);
	} else {
		eyeThreatId_ = Threat::Announce(notice);
	}

	if (finished) {
		SetBeamAttack(false);
		Threat::Withdraw(eyeThreatId_);
		eyeThreatId_ = 0;
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::EyeDetach(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	GuardianBody* body = GetComponent<GuardianBody>();
	if (!owner_ || !rig || !body) {
		return BahamutAI::BTStatus::Failure;
	}

	float duration = (std::max)(params.GetFloat("duration", 0.9f), 1.0e-3f);
	float eyeHeight = params.GetFloat("eyeHeight", 0.9f);
	float legLift = params.GetFloat("legLift", 2.6f);
	float legSpread = params.GetFloat("legSpread", 2.6f);

	bool starting = (currentPhase_ != "EyeDetach");
	if (starting) {
		// 今その脚がある場所を起点にするので、いきなり飛ばない。
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveTargetLocal(index, ToRootLocal(rig->GetFootWorld(index)));
		}
		// 目を土台から切り離す。ここから先、目の高さは土台の車高を無視する。
		eyeMergeStartY_ = body->GetEyeObject() ? body->GetEyeObject()->GetTransform().translation_.y : beamHeight_;
		// **切り離す前の高さ = 合体で戻る先。** 降ろす前に控えておかないと戻り先が分からなくなる。
		eyeAttachedY_ = eyeMergeStartY_;
		body->SetEyeDetached(true);
	}

	float progress = 0.0f;
	bool finished = TickPhase("EyeDetach", duration, context.deltaTime, progress);
	float eased = SmoothStep(progress);

	// 目は地面へ、脚は宙へ。**同じ進捗で反対方向へ動かす**ので「割れて分かれた」ように見える。
	float groundLocalY = SampleGroundUnderRoot() - owner_->GetTransform().translation_.y + eyeHeight;
	body->SetEyeOffset({0.0f, std::lerp(eyeMergeStartY_, groundLocalY, eased), 0.0f});

	for (int index = 0; index < rig->GetLegCount(); ++index) {
		rig->SetCurveWeight(index, eased);
		Vector3 lifted = SpreadLocal(index, legSpread, legLift);
		rig->SetCurveTargetLocal(index, GuardianRigMath::Lerp3(rig->GetCurveTargetLocal(index), lifted, eased * 0.4f));
	}
	RequestEmissive(activeEmissiveIntensity_);

	return finished ? BahamutAI::BTStatus::Success : BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::EyeMerge(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	GuardianBody* body = GetComponent<GuardianBody>();
	if (!owner_ || !rig || !body) {
		return BahamutAI::BTStatus::Failure;
	}

	float duration = (std::max)(params.GetFloat("duration", 0.8f), 1.0e-3f);

	bool starting = (currentPhase_ != "EyeMerge");
	if (starting) {
		eyeMergeStartY_ = body->GetEyeOffset().y;
	}

	float progress = 0.0f;
	bool finished = TickPhase("EyeMerge", duration, context.deltaTime, progress);
	float eased = SmoothStep(progress);

	// 目を定位置へ戻し、脚のweightを落として歩行へ返す。
	// weightが0.5を切った時点で歩行側が主導権を取り戻し、その場から踏み直す。
	//
	// **行き先は 0 ではなく、切り離す前の高さ。**
	// 分離中の目の高さはルート基準なので、0 は足元(地面の中)を指す。
	// そこまで降ろしてから ResetEye() で親子に戻すと、頭の位置へ跳んで見える。
	body->SetEyeOffset({0.0f, std::lerp(eyeMergeStartY_, eyeAttachedY_, eased), 0.0f});
	for (int index = 0; index < rig->GetLegCount(); ++index) {
		rig->SetCurveWeight(index, 1.0f - eased);
	}
	RequestEmissive(activeEmissiveIntensity_ * (1.0f - eased));

	if (finished) {
		// **分離は一時的な状態なので、必ずここで畳む。**
		body->ResetEye();
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveWeight(index, 0.0f);
		}
		HideBeam();
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

// ---------------------------------------------------------------------------
// BT Actions: 第2形態(空中戦)
//
// **地面に予告の円や線は描かない。** 読ませるのはボスの動きだけなので、
// どのアクションも「はっきり見える溜め」→「本番」の2段で書いてある。
// 溜めの形を攻撃ごとに変える(目が縮む/膨らむ、脚が後ろへ引く)ことが、そのまま識別子になる。
//
// 味方AI向けの予告(ThreatBoard)は従来どおり出す。あちらは画面に何も描かない内部データで、
BahamutAI::BTStatus GuardianBossComponent::EyeHover(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GuardianBody* body = GetComponent<GuardianBody>();
	if (!owner_ || !body) {
		return BahamutAI::BTStatus::Failure;
	}

	float height = params.GetFloat("height", 7.0f);
	float orbitRadius = params.GetFloat("orbitRadius", 10.0f);
	float orbitSpeed = params.GetFloat("orbitSpeed", 0.75f);

	// **目は土台から切り離したまま。** 第2形態はこれが常態なので、毎Tick入れ直して
	// 何かの拍子に戻ってしまわないようにする。
	body->SetEyeDetached(true);

	GameObject* target = FindTarget();

	// **目は脚に付いて回らない。** 基準にするのは脚ではなく対象で、
	// 対象を挟んで脚の反対側へ回り込む。挟み撃ちの形になるので、
	// 「脚と目は別の敵だ」ということが動きだけで伝わる。
	Vector3 anchor = target ? target->GetTransform().translation_ : arenaCenter_;
	anchor.y = 0.0f;

	{
		Vector3 legsFromAnchor = owner_->GetTransform().translation_ - anchor;
		legsFromAnchor.y = 0.0f;
		float legsDistance = Length(legsFromAnchor);
		float step = orbitSpeed * context.deltaTime;
		if (legsDistance > 0.05f) {
			// **脚から見て斜め横に付く。** 真後ろ(+π)まで回り込ませると、
			// 脚に注目しているプレイヤーからは目が常に画面外になり、
			// 見えない場所から撃たれる相手になってしまう。
			// 斜めに置けば、脚と目が同時に視界へ入りつつ「別の方向から来る」圧は残る。
			// 近い側の肩へ付くので、目が背後を横切って反対側へ渡ることもない。
			float legsAngle = std::atan2(legsFromAnchor.x, legsFromAnchor.z);
			float flank = params.GetFloat("flankAngle", 62.0f) * kDegreeToRadian;
			float left = std::remainder(legsAngle + flank - eyeOrbitAngle_, 2.0f * std::numbers::pi_v<float>);
			float right = std::remainder(legsAngle - flank - eyeOrbitAngle_, 2.0f * std::numbers::pi_v<float>);
			float diff = (std::abs(left) <= std::abs(right)) ? left : right;
			// 行き着いても止めずに回り続けるので、肩の位置を軸にゆっくり左右へ振れる。
			eyeOrbitAngle_ += (diff >= 0.0f) ? step : -step;
		} else {
			eyeOrbitAngle_ += step;
		}
		eyeOrbitAngle_ = std::remainder(eyeOrbitAngle_, 2.0f * std::numbers::pi_v<float>);
	}

	Vector3 desiredWorld = anchor;
	desiredWorld.x += std::sin(eyeOrbitAngle_) * orbitRadius;
	desiredWorld.z += std::cos(eyeOrbitAngle_) * orbitRadius;
	desiredWorld.y = SampleGroundUnderRoot() + height;

	// 闘技場の外へ出ない。出た先は絵が無いので、外周で止めて内側を向かせる。
	if (arenaRadius_ > 0.0f) {
		Vector3 fromCenter = desiredWorld - arenaCenter_;
		fromCenter.y = 0.0f;
		float distance = Length(fromCenter);
		if (distance > arenaRadius_ && distance > 0.0001f) {
			Vector3 clamped = arenaCenter_ + fromCenter * (arenaRadius_ / distance);
			desiredWorld.x = clamped.x;
			desiredWorld.z = clamped.z;
		}
	}

	// **目が独立オブジェクトならワールド座標をそのまま渡す。**
	// 親子のままの構成(第1形態)ではルート基準ローカルへ落としてから渡す。
	Vector3 desired = body->IsEyeDetachedObject() ? desiredWorld : ToRootLocal(desiredWorld);
	Vector3 current = body->GetEyeOffset();

	// **位置ではなく速度を積む(バネ + 抵抗)。**
	// 目標点へ補間で寄せる書き方だと、狙いが切り替わった瞬間や、
	// 攻撃アクションが目を別の場所へ置いた直後に、次のフレームで大きく飛ぶ。
	// 見ている側にはこれが「瞬間移動」に見える。
	// 加速度で動かせば、行き先が変わっても速度が連続しているので必ず「移動して」見える。
	// 追い越して少し戻る揺り返しも出るので、浮いているものらしい重さが付く。
	float stiffness = params.GetFloat("stiffness", 3.2f);
	float damping = params.GetFloat("damping", 3.4f);
	float maxSpeed = params.GetFloat("maxSpeed", 11.0f);
	Vector3 toDesired = desired - current;
	eyeVelocity_.x += (toDesired.x * stiffness - eyeVelocity_.x * damping) * context.deltaTime;
	eyeVelocity_.y += (toDesired.y * stiffness - eyeVelocity_.y * damping) * context.deltaTime;
	eyeVelocity_.z += (toDesired.z * stiffness - eyeVelocity_.z * damping) * context.deltaTime;
	float speed = Length(eyeVelocity_);
	if (speed > maxSpeed && speed > 0.0001f) {
		eyeVelocity_ = eyeVelocity_ * (maxSpeed / speed);
	}
	body->SetEyeOffset(current + eyeVelocity_ * context.deltaTime);

	// **大技を出している間は目の向きも弾も漂いに任せない。**
	// ここで毎Tick対象へ向け直すと、狙いを固定して撃つ攻撃(ビーム系)が
	// 必ず当たる攻撃になってしまい、見切る余地が消える。
	if (body->IsEyeDetachedObject() && currentPhase_.empty()) {
		body->SetEyeYaw(YawFromEyeToTarget());
		EyeSnipe(context, params, body, target);
	}

	RotateTowardsTarget(params.GetFloat("turnSpeed", 1.6f), context.deltaTime);
	return BahamutAI::BTStatus::Success;
}

/// <summary>
/// **漂いながら撃つ小さな弾。** 脚が攻撃している間も目は黙っていない、という圧を作るための常時攻撃。
/// 大技(星屑・胞子・ビーム)と違って前隙は短く、そのぶん damage も小さい。
/// snipeInterval を 0 以下にすると撃たなくなる。
/// </summary>
void GuardianBossComponent::EyeSnipe(
    BahamutAI::AIContext& context, const BahamutAI::NodeParams& params, GuardianBody* body, KujataEngine::GameObject* target) {
	float interval = params.GetFloat("snipeInterval", 2.6f);
	if (interval <= 0.0f || !target || !body) {
		return;
	}

	eyeSnipeTimer_ -= context.deltaTime;
	if (eyeSnipeTimer_ > 0.0f) {
		return;
	}
	eyeSnipeTimer_ = interval;

	GameObject* eye = body->GetEyeObject();
	Vector3 origin = eye ? GuardianRigMath::ComputeWorldPose(eye).position : body->GetEyeOffset();

	float speed = params.GetFloat("snipeSpeed", 15.0f);
	float damage = params.GetFloat("snipeDamage", 9.0f);
	float radius = params.GetFloat("snipeRadius", 0.9f);

	Vector3 aim = target->GetTransform().translation_;
	aim.y += 1.0f;
	Vector3 toTarget = aim - origin;
	float distance = Length(toTarget);
	if (distance < 0.0001f) {
		return;
	}
	Vector3 velocity = toTarget * (speed / distance);
	float travel = distance / (std::max)(speed, 0.01f);
	SpawnOrb(origin, velocity, radius, travel + 1.2f, false, damage);

	// **予告は着弾点の小さな円。** 前隙が短いぶん半径も小さく、
	// 回避というより「立ち位置をずらせば当たらない」たぐいの圧にしてある。
	Threat::Notice notice;
	notice.source = owner_;
	notice.shape = Threat::Shape::Circle;
	notice.element = Threat::Element::Magic;
	notice.origin = aim;
	notice.origin.y = SampleGroundUnderRoot();
	notice.radius = radius + 1.4f;
	notice.hitTime = travel;
	notice.clearTime = travel + 0.3f;
	notice.damage = damage;
	Threat::Withdraw(eyeSnipeThreatId_);
	eyeSnipeThreatId_ = Threat::Announce(notice);
}

BahamutAI::BTStatus GuardianBossComponent::LegSpinChase(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig) {
		return BahamutAI::BTStatus::Failure;
	}

	float windup = (std::max)(params.GetFloat("windup", 1.0f), 1.0e-3f);
	float duration = (std::max)(params.GetFloat("duration", 3.2f), 1.0e-3f);
	float spread = params.GetFloat("spread", 1.35f);

	bool starting = (currentPhase_ != "LegSpinChase");
	float progress = 0.0f;
	bool finished = TickPhase("LegSpinChase", windup + duration, context.deltaTime, progress);
	float elapsed = phaseTimer_;

	if (elapsed < windup) {
		// **溜め: 脚をゆっくり外へ開く。** 「今から回る」ことがこの一動作で分かる。
		float t = SmoothStep(elapsed / windup);
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveWeight(index, t);
			Vector3 home = rig->GetHomeLocal(index);
			// **地面に吸わせたまま**広げる。y=0 のまま外へ出すので、浮き上がらない。
			Vector3 opened = {home.x * spread, 0.0f, home.z * spread};
			rig->SetCurveTargetLocal(index, GuardianRigMath::Lerp3(rig->GetCurveTargetLocal(index), opened, t));
		}
		RotateTowardsTarget(params.GetFloat("turnSpeed", 2.2f), context.deltaTime);
		RequestEmissive(activeEmissiveIntensity_ * (0.4f + 0.6f * t));
		return BahamutAI::BTStatus::Running;
	}

	// **本番: 開いた脚を接地させたまま回し、相手を追いかける。**
	// 回っている脚そのものが刃なので、判定は脚の先端 + 追従する円で拾う。
	SetAllStrikesActive(true);
	SetAllStrikeScales(params.GetFloat("strikeScale", 2.0f));
	if (starting || !shockwaveHold_) {
		Vector3 center = owner_->GetTransform().translation_;
		center.y = SampleGroundUnderRoot() + 0.4f;
		float bladeRadius = params.GetFloat("waveRadius", 5.0f);
		// duration<=0 = 止めるまで出しっぱなし。回っている間ずっと刃が付いて回る。
		StartShockwave(center, bladeRadius, bladeRadius, 0.0f, true, params.GetFloat("waveDamage", 7.0f),
		    params.GetFloat("waveKnockback", 9.0f), params.GetFloat("waveStun", 0.5f), params.GetFloat("waveHitInterval", 0.8f));
	}

	for (int index = 0; index < rig->GetLegCount(); ++index) {
		rig->SetCurveWeight(index, 1.0f);
		Vector3 home = rig->GetHomeLocal(index);
		rig->SetCurveTargetLocal(index, {home.x * spread, 0.0f, home.z * spread});
	}

	// 体ごと回す。RotateTowardsTarget は使わない — 狙いではなく**回転そのもの**が攻撃なので。
	owner_->GetTransform().rotation_.y += params.GetFloat("spinSpeed", 200.0f) * kDegreeToRadian * context.deltaTime;

	// 回りながら詰める。真っ直ぐ突っ込ませないので、横へ走れば振り切れる。
	if (GameObject* target = FindTarget()) {
		Vector3 toTarget = target->GetTransform().translation_ - owner_->GetTransform().translation_;
		toTarget.y = 0.0f;
		float distance = Length(toTarget);
		float stopDistance = params.GetFloat("stopDistance", 2.5f);
		if (distance > stopDistance && distance > 0.0001f) {
			float speed = params.GetFloat("speed", 2.6f);
			owner_->GetTransform().translation_ += toTarget * (speed * context.deltaTime / distance);
		}
	}
	owner_->GetTransform().translation_.y = SampleGroundUnderRoot();
	RequestEmissive(activeEmissiveIntensity_);

	if (finished) {
		StopShockwave();
		SetAllStrikesActive(false);
		SetAllStrikeScales(1.0f);
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveWeight(index, 0.0f);
		}
		Threat::SetOpen(owner_, params.GetFloat("openSeconds", 2.0f));
		return BahamutAI::BTStatus::Success;
	}

	// 味方AIへは「自分に付いて回る円」として伝える。追従するので逃げ続けるしかない。
	{
		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Circle;
		notice.element = Threat::Element::Physical;
		notice.origin = owner_->GetTransform().translation_;
		notice.radius = params.GetFloat("waveRadius", 5.0f);
		notice.followSource = true;
		notice.sustained = true;
		notice.hitTime = 0.0f;
		notice.clearTime = (std::max)(windup + duration - elapsed, 0.05f);
		notice.damage = params.GetFloat("waveDamage", 7.0f);
		if (attackThreatId_ > 0) {
			Threat::Amend(attackThreatId_, notice);
		} else {
			attackThreatId_ = Threat::Announce(notice);
		}
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::EyeHomingVolley(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GuardianBody* body = GetComponent<GuardianBody>();
	GameObject* eye = body ? body->GetEyeObject() : nullptr;
	if (!owner_ || !body || !eye) {
		return BahamutAI::BTStatus::Failure;
	}

	float windup = (std::max)(params.GetFloat("windup", 1.1f), 1.0e-3f);
	float fire = (std::max)(params.GetFloat("fire", 1.4f), 1.0e-3f);
	int count = static_cast<int>(params.GetFloat("count", 5.0f));

	bool starting = (currentPhase_ != "EyeHomingVolley");
	if (starting) {
		volleyIndex_ = 0;
	}

	float progress = 0.0f;
	bool finished = TickPhase("EyeHomingVolley", windup + fire, context.deltaTime, progress);
	float elapsed = phaseTimer_;

	if (elapsed < windup) {
		// **溜め: 目が細かく震える。** 星屑(縮む)や振り下ろし(構える)と混ざらない動きにしてある。
		float t = SmoothStep(elapsed / windup);
		float shiver = 1.0f + 0.06f * std::sin(elapsed * 40.0f);
		eye->GetTransform().scale_ = {shiver, shiver, shiver};
		RequestEmissive(activeEmissiveIntensity_ * (0.4f + 0.6f * t));
		RotateTowardsTarget(params.GetFloat("turnSpeed", 2.0f), context.deltaTime);
		return BahamutAI::BTStatus::Running;
	}

	eye->GetTransform().scale_ = {1.0f, 1.0f, 1.0f};
	RequestEmissive(activeEmissiveIntensity_);

	// count発を fire 秒に均等割りして撃つ。撃つたびに狙い直すので、固まっていると刺さる。
	int wanted = static_cast<int>(((elapsed - windup) / fire) * static_cast<float>(count)) + 1;
	wanted = (std::min)(wanted, count);
	while (volleyIndex_ < wanted) {
		GameObject* target = FindTarget();
		if (!target) {
			break;
		}
		Vector3 origin = GuardianRigMath::ComputeWorldPose(eye).position;
		Vector3 aim = target->GetTransform().translation_;
		aim.y += 1.0f;
		Vector3 toTarget = aim - origin;
		float distance = Length(toTarget);
		if (distance < 0.0001f) {
			break;
		}
		float speed = params.GetFloat("speed", 11.0f);
		// **初速は真っ直ぐ相手へ向けず、少し横へ散らす。**
		// 撃った瞬間から一直線だと避ける余地が無い。曲がってくるからこそ「振り切る」遊びになる。
		float angle = static_cast<float>(volleyIndex_) * 2.39996f;
		Vector3 side = {std::cos(angle), 0.0f, -std::sin(angle)};
		Vector3 velocity = toTarget * (speed / distance) + side * (speed * params.GetFloat("spread", 0.45f));
		float life = params.GetFloat("life", 4.0f);
		SpawnOrb(origin, velocity, params.GetFloat("radius", 0.8f), life, false, params.GetFloat("damage", 6.0f), target,
		    params.GetFloat("turnRate", 1.6f));

		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Circle;
		notice.element = Threat::Element::Magic;
		notice.origin = aim;
		notice.origin.y = SampleGroundUnderRoot();
		notice.radius = params.GetFloat("radius", 0.8f) + 1.6f;
		notice.hitTime = distance / (std::max)(speed, 0.01f);
		notice.clearTime = notice.hitTime + 0.6f;
		notice.damage = params.GetFloat("damage", 6.0f);
		Threat::Announce(notice);

		++volleyIndex_;
	}

	if (finished) {
		Threat::SetOpen(owner_, params.GetFloat("openSeconds", 2.0f));
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::EyeLaserBurst(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GuardianBody* body = GetComponent<GuardianBody>();
	if (!owner_ || !body) {
		return BahamutAI::BTStatus::Failure;
	}

	float windup = (std::max)(params.GetFloat("windup", 0.9f), 1.0e-3f);
	int shots = (std::max)(static_cast<int>(params.GetFloat("shots", 3.0f)), 1);
	float shotTime = (std::max)(params.GetFloat("shotTime", 0.55f), 0.05f);
	float length = params.GetFloat("length", 40.0f);
	float thickness = params.GetFloat("thickness", 0.4f);
	float pitch = params.GetFloat("pitch", 6.0f) * kDegreeToRadian;

	bool starting = (currentPhase_ != "EyeLaserBurst");
	if (starting) {
		volleyIndex_ = 0;
		eyeAimYaw_ = YawFromEyeToTarget();
	}

	float progress = 0.0f;
	bool finished = TickPhase("EyeLaserBurst", windup + shotTime * static_cast<float>(shots), context.deltaTime, progress);
	float elapsed = phaseTimer_;

	if (elapsed < windup) {
		// **溜め: 狙いを付けるだけ。** ビームの器はまだ出さない。
		float t = SmoothStep(elapsed / windup);
		eyeAimYaw_ = YawFromEyeToTarget();
		body->SetEyeYaw(eyeAimYaw_);
		RequestEmissive(activeEmissiveIntensity_ * (0.3f + 0.7f * t));
		HideBeam();
		return BahamutAI::BTStatus::Running;
	}

	// **1発ごとに「狙い直す→撃つ」を繰り返す。**
	// 撃っている間は狙いを固定するので、走り続けていれば置いていける。
	float inShot = elapsed - windup - static_cast<float>(volleyIndex_) * shotTime;
	float aimTime = shotTime * 0.55f;
	if (inShot < aimTime) {
		// 狙い付け: 細い線で相手を追う(まだ判定は出さない)。
		eyeAimYaw_ = YawFromEyeToTarget();
		body->SetEyeYaw(eyeAimYaw_);
		SetBeamAttack(false);
		if (GameObject* beam = AcquireBeam()) {
			beam->SetActive(true);
		}
		UpdateEyeBeam(eyeAimYaw_, pitch, length, thickness * 0.25f);
		Threat::Withdraw(eyeThreatId_);
		eyeThreatId_ = 0;
	} else {
		// 発射: 狙いは固定。太くして判定を出す。
		body->SetEyeYaw(eyeAimYaw_);
		SetBeamAttack(true);
		UpdateEyeBeam(eyeAimYaw_, pitch, length, thickness);

		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Line;
		notice.element = Threat::Element::Magic;
		notice.origin = GetEyeWorldPosition();
		notice.origin.y = SampleGroundUnderRoot();
		notice.direction = {std::sin(eyeAimYaw_), 0.0f, std::cos(eyeAimYaw_)};
		notice.radius = thickness * 2.0f;
		notice.length = length;
		notice.sustained = true;
		notice.hitTime = 0.0f;
		notice.clearTime = (std::max)(shotTime - inShot, 0.05f);
		notice.damage = params.GetFloat("damage", 8.0f);
		if (eyeThreatId_ > 0) {
			Threat::Amend(eyeThreatId_, notice);
		} else {
			eyeThreatId_ = Threat::Announce(notice);
		}
	}
	if (inShot >= shotTime) {
		++volleyIndex_;
	}
	RequestEmissive(activeEmissiveIntensity_);

	if (finished) {
		HideBeam();
		Threat::Withdraw(eyeThreatId_);
		eyeThreatId_ = 0;
		Threat::SetOpen(owner_, params.GetFloat("openSeconds", 2.0f));
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::LegsCrawl(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	if (!owner_) {
		return BahamutAI::BTStatus::Failure;
	}
	GameObject* target = FindTarget();
	if (!target) {
		return BahamutAI::BTStatus::Failure;
	}

	Vector3 toTarget = target->GetTransform().translation_ - owner_->GetTransform().translation_;
	toTarget.y = 0.0f;
	float distance = Length(toTarget);
	float stopDistance = params.GetFloat("stopDistance", 6.0f);
	if (distance > stopDistance && distance > 0.0001f) {
		float speed = params.GetFloat("speed", 3.0f);
		owner_->GetTransform().translation_ += toTarget * (speed * context.deltaTime / distance);
	}
	return BahamutAI::BTStatus::Success;
}

BahamutAI::BTStatus GuardianBossComponent::EyeStarfall(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GuardianBody* body = GetComponent<GuardianBody>();
	GameObject* eye = body ? body->GetEyeObject() : nullptr;
	if (!owner_ || !body || !eye) {
		return BahamutAI::BTStatus::Failure;
	}

	float windup = (std::max)(params.GetFloat("windup", 1.1f), 1.0e-3f);
	float fire = (std::max)(params.GetFloat("fire", 1.6f), 1.0e-3f);
	int count = static_cast<int>(params.GetFloat("count", 8.0f));
	float spread = params.GetFloat("spread", 7.0f);
	float height = params.GetFloat("height", 14.0f);

	bool starting = (currentPhase_ != "EyeStarfall");
	if (starting) {
		volleyIndex_ = 0;
	}

	float progress = 0.0f;
	bool finished = TickPhase("EyeStarfall", windup + fire, context.deltaTime, progress);
	float elapsed = phaseTimer_;

	if (elapsed < windup) {
		// **溜め: 目がぐっと縮んで沈黙する。** 星屑はこの「縮む」動きが合図。
		float t = SmoothStep(elapsed / windup);
		eye->GetTransform().scale_ = {1.0f - 0.35f * t, 1.0f - 0.35f * t, 1.0f - 0.35f * t};
		RequestEmissive(activeEmissiveIntensity_ * (0.4f + 0.6f * t));
		RotateTowardsTarget(params.GetFloat("turnSpeed", 2.0f), context.deltaTime);
		return BahamutAI::BTStatus::Running;
	}

	// **本番: 縮んだぶんが弾ける。** 溜めの逆の動きにして、切り替わりを見た目で分からせる。
	eye->GetTransform().scale_ = {1.15f, 1.15f, 1.15f};
	RequestEmissive(activeEmissiveIntensity_);

	// count発を fire 秒に均等割りして落とす。**1発ずつ間を空ける**ので、
	// 落ちてくる球そのものが「次はここ」の合図になる(地面の予告円は要らない)。
	int wanted = static_cast<int>(((elapsed - windup) / fire) * static_cast<float>(count)) + 1;
	wanted = (std::min)(wanted, count);
	while (volleyIndex_ < wanted) {
		Vector3 origin = GuardianRigMath::ComputeWorldPose(eye).position;
		Vector3 landing = origin;
		if (GameObject* target = FindTarget()) {
			landing = target->GetTransform().translation_;
		}
		// 対象の周りへ散らす。角度は発数から決めるので毎回同じ形にならない。
		float angle = static_cast<float>(volleyIndex_) * 2.39996f; // 黄金角。重なりにくい並びになる
		float radius = spread * (0.25f + 0.75f * static_cast<float>(volleyIndex_ % 3) / 2.0f);
		landing.x += std::sin(angle) * radius;
		landing.z += std::cos(angle) * radius;
		landing.y = SampleGroundUnderRoot();

		// 高い位置から落とす。滞空時間があるので、見てから動けば避けられる。
		Vector3 start = {landing.x, landing.y + height, landing.z};
		float fallTime = std::sqrt((std::max)(2.0f * height / 18.0f, 0.01f));
		SpawnOrb(start, Vector3{0.0f, 0.0f, 0.0f}, params.GetFloat("radius", 1.1f), fallTime + 0.4f, false,
		    params.GetFloat("damage", 12.0f));

		// 味方AI向けの予告(画面には出ない)。
		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Circle;
		notice.element = Threat::Element::Magic;
		notice.origin = landing;
		notice.radius = params.GetFloat("radius", 1.1f) + 0.8f;
		notice.hitTime = fallTime;
		notice.clearTime = fallTime + 0.3f;
		notice.damage = params.GetFloat("damage", 12.0f);
		Threat::Announce(notice);

		++volleyIndex_;
	}

	if (finished) {
		eye->GetTransform().scale_ = {1.0f, 1.0f, 1.0f};
		// 撃ち終わりが隙。目は縮むだけで庇えない。
		Threat::SetOpen(owner_, params.GetFloat("openSeconds", 1.2f));
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::EyeSporeBurst(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GuardianBody* body = GetComponent<GuardianBody>();
	GameObject* eye = body ? body->GetEyeObject() : nullptr;
	if (!owner_ || !body || !eye) {
		return BahamutAI::BTStatus::Failure;
	}

	float windup = (std::max)(params.GetFloat("windup", 1.0f), 1.0e-3f);
	float recover = (std::max)(params.GetFloat("recover", 0.6f), 1.0e-3f);
	int count = static_cast<int>(params.GetFloat("count", 7.0f));

	bool starting = (currentPhase_ != "EyeSporeBurst");
	if (starting) {
		volleyIndex_ = 0;
	}

	float progress = 0.0f;
	bool finished = TickPhase("EyeSporeBurst", windup + recover, context.deltaTime, progress);
	float elapsed = phaseTimer_;

	if (elapsed < windup) {
		// **溜め: 目が大きく膨らむ。** 星屑(縮む)とは逆の動きなので、見分けが付く。
		float t = SmoothStep(elapsed / windup);
		float scale = 1.0f + 0.55f * t;
		eye->GetTransform().scale_ = {scale, scale, scale};
		RequestEmissive(activeEmissiveIntensity_ * (0.4f + 0.6f * t));
		RotateTowardsTarget(params.GetFloat("turnSpeed", 1.5f), context.deltaTime);
		return BahamutAI::BTStatus::Running;
	}

	// **本番: 膨らみきった目が一気に萎んで、放物線で胞子をばら撒く。**
	if (volleyIndex_ == 0) {
		Vector3 origin = GuardianRigMath::ComputeWorldPose(eye).position;
		float lingerSeconds = params.GetFloat("linger", 5.0f);
		float speed = params.GetFloat("speed", 11.0f);

		for (int index = 0; index < count; ++index) {
			float angle = (static_cast<float>(index) / static_cast<float>((std::max)(count, 1))) * 2.0f * std::numbers::pi_v<float>;
			Vector3 velocity = {std::sin(angle) * speed, params.GetFloat("lift", 6.0f), std::cos(angle) * speed};
			SpawnOrb(origin, velocity, params.GetFloat("radius", 1.4f), lingerSeconds, true, params.GetFloat("damage", 10.0f));
		}
		// 残留域はボスの周りに広がる。味方AIへは「持続する円」として伝える。
		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Circle;
		notice.element = Threat::Element::Magic;
		notice.origin = origin;
		notice.origin.y = SampleGroundUnderRoot();
		notice.radius = params.GetFloat("threatRadius", 8.0f);
		notice.sustained = true;
		notice.hitTime = 0.6f;
		notice.clearTime = lingerSeconds;
		notice.damage = params.GetFloat("damage", 10.0f);
		Threat::Withdraw(eyeThreatId_);
		eyeThreatId_ = Threat::Announce(notice);
		volleyIndex_ = 1;
	}

	float t = SmoothStep(std::clamp((elapsed - windup) / recover, 0.0f, 1.0f));
	float scale = std::lerp(1.55f, 1.0f, t);
	eye->GetTransform().scale_ = {scale, scale, scale};
	RequestEmissive(activeEmissiveIntensity_ * (1.0f - t * 0.6f));

	if (finished) {
		eye->GetTransform().scale_ = {1.0f, 1.0f, 1.0f};
		// 撃ち終わりが隙。目は縮むだけで庇えない。
		Threat::SetOpen(owner_, params.GetFloat("openSeconds", 1.2f));
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::LegCharge(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig) {
		return BahamutAI::BTStatus::Failure;
	}

	float windup = (std::max)(params.GetFloat("windup", 0.85f), 1.0e-3f);
	float dash = (std::max)(params.GetFloat("dash", 0.7f), 1.0e-3f);
	float speed = params.GetFloat("speed", 22.0f);

	bool starting = (currentPhase_ != "LegCharge");
	if (starting) {
		// 溜めの間に狙いを固定する。**突進中は追尾しない**ので、横へ抜ければ必ずかわせる。
		eyeAimYaw_ = YawFromEyeToTarget();
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveTargetLocal(index, ToRootLocal(rig->GetFootWorld(index)));
		}
	}

	float progress = 0.0f;
	bool finished = TickPhase("LegCharge", windup + dash, context.deltaTime, progress);
	float elapsed = phaseTimer_;

	if (elapsed < windup) {
		// **溜め: 脚を後ろへ引き絞って body を沈める。** 弓を引くように見えるので、
		// 「まっすぐ来る」と分かる。ここで狙いの向きも見えている。
		float t = SmoothStep(elapsed / windup);
		RotateTowardsTarget(params.GetFloat("turnSpeed", 3.0f), context.deltaTime);
		eyeAimYaw_ = YawFromEyeToTarget();
		SetBodySink(-params.GetFloat("sink", 0.9f) * t);
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveWeight(index, t);
			// 定位置より後ろ(-Z)へ寄せる = 踏ん張って溜めている形。
			Vector3 home = rig->GetHomeLocal(index);
			Vector3 coiled = {home.x * 0.8f, home.y, home.z - params.GetFloat("pull", 1.2f)};
			// **目標そのものを補間する。** weightだけ動かして目標を即座に置き換えると、
			// 溜めに入った1フレーム目で脚が飛ぶ(切り替わりが硬く見える原因)。
			rig->SetCurveTargetLocal(index, GuardianRigMath::Lerp3(rig->GetCurveTargetLocal(index), coiled, t));
		}
		RequestEmissive(activeEmissiveIntensity_ * 0.8f);

		if (elapsed + context.deltaTime >= windup) {
			// 走り出す直前に予告(味方AI向け・画面には出ない)。線として伝える。
			Threat::Notice notice;
			notice.source = owner_;
			notice.shape = Threat::Shape::Line;
			notice.element = Threat::Element::Physical;
			notice.origin = owner_->GetTransform().translation_;
			notice.direction = {std::sin(eyeAimYaw_), 0.0f, std::cos(eyeAimYaw_)};
			notice.radius = params.GetFloat("width", 3.0f);
			notice.length = speed * dash;
			notice.hitTime = 0.12f;
			notice.clearTime = dash;
			notice.damage = params.GetFloat("damage", 18.0f);
			Threat::Withdraw(attackThreatId_);
			attackThreatId_ = Threat::Announce(notice);
		}
		return BahamutAI::BTStatus::Running;
	}

	// **本番: 引き絞りを解いて地を這って突っ込む。** 脚は前へ投げ出す。
	// 投げ出しも補間する — 溜めの姿勢から一瞬で伸び切ると、動きの繋がりが読めない。
	float dashT = SmoothStep(std::clamp((elapsed - windup) / dash, 0.0f, 1.0f));
	SetBodySink(-0.35f);
	SetAllStrikesActive(true);
	SetAllStrikeScales(params.GetFloat("strikeScale", 2.0f));
	for (int index = 0; index < rig->GetLegCount(); ++index) {
		rig->SetCurveWeight(index, 1.0f);
		Vector3 home = rig->GetHomeLocal(index);
		Vector3 thrown = {home.x, params.GetFloat("legHeight", 0.4f), home.z + params.GetFloat("reach", 1.6f)};
		rig->SetCurveTargetLocal(index, GuardianRigMath::Lerp3(rig->GetCurveTargetLocal(index), thrown, 0.25f + 0.5f * dashT));
	}
	Vector3 forward = {std::sin(eyeAimYaw_), 0.0f, std::cos(eyeAimYaw_)};
	owner_->GetTransform().translation_ += forward * (speed * context.deltaTime);
	owner_->GetTransform().translation_.y = SampleGroundUnderRoot();
	RequestEmissive(activeEmissiveIntensity_);

	if (finished) {
		SetAllStrikesActive(false);
		SetAllStrikeScales(1.0f);
		SetBodySink(0.0f);
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveWeight(index, 0.0f);
		}
		// 走り抜けた直後は止まれない。ここが突進の後隙。
		Threat::SetOpen(owner_, params.GetFloat("openSeconds", 1.4f));
		Threat::Withdraw(attackThreatId_);
		attackThreatId_ = 0;
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GuardianBossComponent::CrossfireCombo(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	IGuardianLegRig* rig = GetRig();
	GuardianBody* body = GetComponent<GuardianBody>();
	GameObject* eye = body ? body->GetEyeObject() : nullptr;
	if (!owner_ || !rig || !body || !eye) {
		return BahamutAI::BTStatus::Failure;
	}

	float windup = (std::max)(params.GetFloat("windup", 1.3f), 1.0e-3f);
	float duration = (std::max)(params.GetFloat("duration", 2.2f), 1.0e-3f);
	float beamLength = params.GetFloat("length", 18.0f);
	float beamThickness = params.GetFloat("thickness", 0.5f);

	bool starting = (currentPhase_ != "CrossfireCombo");
	if (starting) {
		eyeAimYaw_ = YawFromEyeToTarget();
		volleyIndex_ = 0;
	}

	float progress = 0.0f;
	bool finished = TickPhase("CrossfireCombo", windup + duration, context.deltaTime, progress);
	float elapsed = phaseTimer_;

	if (elapsed < windup) {
		// **溜め: 目が縮み、同時に脚が引き絞られる。**
		// 「2つの溜めが同時に見える」こと自体がコンボの合図になっている。
		float t = SmoothStep(elapsed / windup);
		float scale = 1.0f - 0.3f * t;
		eye->GetTransform().scale_ = {scale, scale, scale};
		SetBodySink(-params.GetFloat("sink", 0.8f) * t);
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveWeight(index, t);
			Vector3 home = rig->GetHomeLocal(index);
			Vector3 coiled = {home.x * 0.8f, home.y, home.z - params.GetFloat("pull", 1.1f)};
			// 溜めの入りを滑らかにする(LegChargeと同じ理由)。
			rig->SetCurveTargetLocal(index, GuardianRigMath::Lerp3(rig->GetCurveTargetLocal(index), coiled, t));
		}
		RotateTowardsTarget(params.GetFloat("turnSpeed", 2.5f), context.deltaTime);
		eyeAimYaw_ = YawFromEyeToTarget();
		RequestEmissive(activeEmissiveIntensity_ * (0.5f + 0.5f * t));
		return BahamutAI::BTStatus::Running;
	}

	float t = std::clamp((elapsed - windup) / duration, 0.0f, 1.0f);
	eye->GetTransform().scale_ = {1.1f, 1.1f, 1.1f};
	RequestEmissive(activeEmissiveIntensity_);

	// --- 目: ビームを一定の速さで薙ぐ ---
	float sweepArc = params.GetFloat("arc", 150.0f) * kDegreeToRadian;
	float beamYaw = eyeAimYaw_ - sweepArc * 0.5f + sweepArc * t;
	float beamPitch = params.GetFloat("pitch", 8.0f) * kDegreeToRadian;
	if (GameObject* beam = AcquireBeam()) {
		beam->SetActive(true);
	}
	SetBeamAttack(true);
	body->SetEyeYaw(body->IsEyeDetachedObject() ? beamYaw : beamYaw - GetRootYaw());
	body->SetEyePitch(beamPitch);
	UpdateEyeBeam(beamYaw, beamPitch, beamLength, beamThickness);

	// --- 脚: その場で暴れる(**突っ込ませない**) ---
	// 以前はここで前進させていたが、走っている絵と当たり判定が噛み合わず
	// 「攻撃している感じがしない」ので、足元を薙ぐ動きに置き換えた。
	// 旋回だけは続けるので、どちらへ向けて暴れているかは読める。
	RotateTowardsTarget(params.GetFloat("turnSpeed", 2.5f) * 0.5f, context.deltaTime);
	owner_->GetTransform().translation_.y = SampleGroundUnderRoot();
	SetBodySink(-0.3f);
	SetAllStrikesActive(true);
	SetAllStrikeScales(params.GetFloat("strikeScale", 2.0f));
	for (int index = 0; index < rig->GetLegCount(); ++index) {
		rig->SetCurveWeight(index, 1.0f);
		Vector3 home = rig->GetHomeLocal(index);
		Vector3 thrown = {home.x, params.GetFloat("legHeight", 0.4f), home.z + params.GetFloat("reach", 1.5f)};
		rig->SetCurveTargetLocal(index, GuardianRigMath::Lerp3(rig->GetCurveTargetLocal(index), thrown, 0.25f + 0.5f * SmoothStep(t)));
	}

	// 味方AIへは**2件同時に**出す。1件しか出さないと、AIも片方しか見ないことになる。
	{
		Threat::Notice beamNotice;
		beamNotice.source = owner_;
		beamNotice.shape = Threat::Shape::Line;
		beamNotice.element = Threat::Element::Magic;
		beamNotice.origin = GetEyeWorldPosition();
		beamNotice.origin.y = SampleGroundUnderRoot();
		beamNotice.direction = {std::sin(beamYaw), 0.0f, std::cos(beamYaw)};
		beamNotice.radius = beamThickness * 2.0f;
		beamNotice.length = beamLength;
		beamNotice.sustained = true;
		beamNotice.hitTime = 0.0f;
		beamNotice.clearTime = (std::max)(duration - (elapsed - windup), 0.05f);
		beamNotice.damage = 12.0f;
		if (eyeThreatId_ > 0) {
			Threat::Amend(eyeThreatId_, beamNotice);
		} else {
			eyeThreatId_ = Threat::Announce(beamNotice);
		}

		Threat::Notice dashNotice;
		dashNotice.source = owner_;
		dashNotice.shape = Threat::Shape::Circle;
		dashNotice.element = Threat::Element::Physical;
		dashNotice.origin = owner_->GetTransform().translation_;
		dashNotice.radius = params.GetFloat("width", 3.2f);
		dashNotice.followSource = true;
		dashNotice.sustained = true;
		dashNotice.hitTime = 0.0f;
		dashNotice.clearTime = (std::max)(duration - (elapsed - windup), 0.05f);
		dashNotice.damage = params.GetFloat("damage", 16.0f);
		if (attackThreatId_ > 0) {
			Threat::Amend(attackThreatId_, dashNotice);
		} else {
			attackThreatId_ = Threat::Announce(dashNotice);
		}
	}

	if (finished) {
		SetBeamAttack(false);
		HideBeam();
		SetAllStrikesActive(false);
		SetAllStrikeScales(1.0f);
		SetBodySink(0.0f);
		eye->GetTransform().scale_ = {1.0f, 1.0f, 1.0f};
		body->SetEyeYaw(0.0f);
		body->SetEyePitch(0.0f);
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveWeight(index, 0.0f);
		}
		// 二段構えを出し切った直後。**一番大きな隙**にしてある。
		Threat::SetOpen(owner_, params.GetFloat("openSeconds", 1.8f));
		Threat::Withdraw(eyeThreatId_);
		Threat::Withdraw(attackThreatId_);
		eyeThreatId_ = 0;
		attackThreatId_ = 0;
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

void GuardianBossComponent::UpdateBeam(float length, float thickness, float aimHeight) {
	// **器はワールド空間に置く。** ボスのビームはPrefabからランタイム生成した親無しの板で、
	// 目のYawで薙ぐ攻撃(EyeBeamSpin)と同じ置き方に揃えてある。
	// ヨーはルートの旋回(RotateTowardsTarget)が担当し、ここはピッチだけ決める。
	// 役割を分けておくと「狙いの追従の遅さ」と「高さが合うか」を別々に調整できる。
	float pitch = 0.0f;
	if (GameObject* target = FindTarget()) {
		Vector3 aimWorld = target->GetTransform().translation_;
		aimWorld.y += aimHeight;

		Vector3 origin = GetEyeWorldPosition();
		Vector3 diff = aimWorld - origin;
		float horizontal = std::sqrt(diff.x * diff.x + diff.z * diff.z);
		// +Zを前とする左手系では、X軸まわりの正の回転が下を向く。だから符号を反転する。
		pitch = std::atan2(-diff.y, (std::max)(horizontal, 1.0e-3f));
	}

	UpdateEyeBeam(GetRootYaw(), pitch, length, thickness);
}

void GuardianBossComponent::SetBeamAttack(bool active) {
	if (GameObject* beam = GetBeamObject()) {
		if (EnemyWeapon* weapon = beam->GetComponent<EnemyWeapon>()) {
			weapon->SetAttack(active);
		}
	}
}

bool GuardianBossComponent::IsBeamPhase(const std::string& phase) {
	// ビームの器を出しているアクション。ここに載っていないフレームは器を畳む。
	return phase == "BeamCharge" || phase == "BeamFire" || phase == "BeamRecover" || phase == "EyeAimUp" ||
	       phase == "EyeBeamSlam" || phase == "EyeBeamSpin" || phase == "CrossfireCombo";
}

void GuardianBossComponent::HideBeam() {
	if (GameObject* beam = GetBeamObject()) {
		if (EnemyWeapon* weapon = beam->GetComponent<EnemyWeapon>()) {
			weapon->SetAttack(false);
		}
		beam->SetActive(false);
	}
}

void GuardianBossComponent::ApplyLegBlades(float reachRatio, float pitchDeg, bool straight) {
	IGuardianLegRig* rig = GetRig();
	if (!rig) {
		return;
	}

	float pitch = pitchDeg * kDegreeToRadian;

	for (int index = 0; index < rig->GetLegCount(); ++index) {
		// **脚は回さない。** 定位置(homeYaw)の向きのまま、自分の接合部からまっすぐ外へ突っ張らせるだけ。
		// 以前は脚ごとに角度をずらして回していたが、接合部の位置と脚の向きが噛み合わないため
		// 4本がバラバラの円を描いて形がいびつになった。回転はルートのYawに一本化する。
		Vector3 home = rig->GetHomeLocal(index);
		float yaw = std::atan2(home.x, home.z);

		Vector3 hipLocal = ToRootLocal(rig->GetHipWorld(index));
		Vector3 direction = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};

		// 最大長いっぱいまで伸ばすと弛みが消えて棒になる。pitch=0なら接合部と同じ高さ=地面と平行。
		float reach = rig->GetMaxReach(index) * std::clamp(reachRatio, 0.05f, 1.0f);

		rig->SetCurveStraight(index, straight);
		rig->SetCurveTargetLocal(index, hipLocal + direction * reach);
	}
}

void GuardianBossComponent::SpinRootYaw(float spinSpeedDeg, float deltaTime) {
	if (!owner_) {
		return;
	}

	// 脚の目標はルート基準のローカル座標で置いてあるので、ルートを回せば脚も胴体も一体で回る。
	// **旋回(RotateTowardsTarget)と同じ場所を書くので、回転中は対象を向かせないこと。**
	float& yaw = owner_->GetTransform().rotation_.y;
	yaw = WrapYawAngle(yaw + spinSpeedDeg * kDegreeToRadian * deltaTime);
}

float GuardianBossComponent::SampleGroundUnderRoot() const {
	GameObject* owner = GetOwner();
	if (!owner) {
		return 0.0f;
	}
	if (GuardianGait* gait = const_cast<GuardianBossComponent*>(this)->GetComponent<GuardianGait>()) {
		return gait->SampleGroundAt(owner->GetTransform().translation_);
	}
	return owner->GetTransform().translation_.y;
}

KujataEngine::Vector3 GuardianBossComponent::TuckedLocal(int legIndex, float pull, float lift) const {
	IGuardianLegRig* rig = const_cast<GuardianBossComponent*>(this)->GetRig();
	if (!rig) {
		return Vector3{0.0f, 0.0f, 0.0f};
	}
	// 定位置の向きは保ったまま内側へ引き寄せ、持ち上げる。向きを変えないので着地で素直に開ける。
	Vector3 home = rig->GetHomeLocal(legIndex);
	Vector3 tucked = home * std::clamp(1.0f - pull, 0.05f, 1.0f);
	tucked.y = lift;
	return tucked;
}

KujataEngine::Vector3 GuardianBossComponent::SpreadLocal(int legIndex, float distance, float height) const {
	IGuardianLegRig* rig = const_cast<GuardianBossComponent*>(this)->GetRig();
	if (!rig) {
		return Vector3{0.0f, 0.0f, 0.0f};
	}
	Vector3 home = rig->GetHomeLocal(legIndex);
	home.y = 0.0f;
	Vector3 direction = GuardianRigMath::SafeNormalize(home, {0.0f, 0.0f, 1.0f});
	Vector3 spread = direction * distance;
	spread.y = height;
	return spread;
}

void GuardianBossComponent::SetAllStrikesActive(bool active) {
	IGuardianLegRig* rig = GetRig();
	if (!rig) {
		return;
	}
	for (int index = 0; index < rig->GetLegCount(); ++index) {
		SetStrikeActive(index, active);
	}
}

KujataEngine::GameObject* GuardianBossComponent::AcquireShockwave() {
	if (shockwave_ || shockwaveTried_) {
		return shockwave_;
	}
	shockwaveTried_ = true;

	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || shockwavePrefabPath_.empty()) {
		return nullptr;
	}
	// ランタイム生成なのでエディタのPrefab関連付けは不要(linkInstance=false)。
	PrefabAsset::InstantiateResult result = PrefabAsset::Instantiate(*scene, shockwavePrefabPath_, false);
	if (!result.succeeded || !result.rootObject) {
		Logger::Log("[GuardianBossComponent] shockwave prefab load failed (" + shockwavePrefabPath_ + "): " + result.message);
		return nullptr;
	}
	shockwave_ = result.rootObject;
	// 減衰の基準になる既定色を覚えておく(以後はこれにαの倍率を掛けて個体へ上書きする)。
	if (ModelRendererComponent* renderer = shockwave_->GetComponent<ModelRendererComponent>()) {
		shockwaveBaseColor_ = renderer->GetBaseColor();
	}
	shockwave_->SetActive(false);
	return shockwave_;
}

void GuardianBossComponent::StartShockwave(const Vector3& center, float startRadius, float endRadius, float duration, bool followOwner,
    float damage, float knockback, float stunDuration, float hitInterval) {
	GameObject* wave = AcquireShockwave();
	// 衝撃波を出す攻撃(ストンプ/薙ぎ払い/跳躍着地/回転刃)は全部ここを通るので、
	// 土埃と同じくボスの重い一撃の音もここ1箇所で鳴らす。
	GameAudio::PlaySe(GameAudio::Se::BossSlam);
	// **土埃はここ1箇所で賄う。** ストンプもレッグスイープもリープ着地も回転刃も
	// 最後はこの関数を通るので、攻撃ごとに書くと必ずどれかを付け忘れる。
	// 強さは波の半径から決める。大きい攻撃ほど濃い土煙が上がる。
	if (owner_) {
		float strength = std::clamp(endRadius / 4.0f, 0.5f, 3.0f);
		GameFx::Burst(owner_->GetScene(), GameFx::Prefab::kDust, center, strength);
	}

	if (!wave) {
		return;
	}

	// 第2形態では波が大きく重くなる。**移行の合図に出す波自体には掛けない**
	// (合図が本気の一撃と同じ威力だと、変わった瞬間に理不尽に死ぬ)。
	if (phase2_ && !emittingPhase2Burst_) {
		endRadius *= phase2RadiusScale_;
		damage *= phase2DamageScale_;
	}

	shockwaveCenter_ = center;
	// **地面からわずかに浮かせる。** 輪は板ポリなので、地面とまったく同じ高さに置くと
	// 深度がぶつかってチラつく(Zファイティング)。ここで一度上げておけば、追従側も
	// この y を引き継ぐので2箇所で同じ調整をしなくて済む。
	shockwaveCenter_.y += kShockwaveGroundLift;
	shockwaveStartRadius_ = startRadius;
	shockwaveEndRadius_ = endRadius;
	shockwaveDuration_ = (std::max)(duration, 0.0f);
	shockwaveTimer_ = shockwaveDuration_;
	shockwaveFollow_ = followOwner;
	// durationが0以下なら「止めるまで出しっぱなし」。コマ回転のように長さをBT側が決める攻撃で使う。
	shockwaveHold_ = (duration <= 0.0f);

	if (EnemyWeapon* weapon = wave->GetComponent<EnemyWeapon>()) {
		weapon->SetHitParams(damage, knockback, stunDuration, hitInterval);
		// OFF→ONの立ち上がりでヒット履歴がクリアされるので、出すたびに当たり直せる。
		weapon->SetAttack(true);
	}

	WorldTransform& transform = wave->GetTransform();
	transform.translation_ = shockwaveCenter_;
	float radius = shockwaveStartRadius_;
	// Ringプリミティブは半径1のXZ円環なので、スケールがそのまま半径になる(見た目・当たりの両方)。
	// 当たりは球のままなので、輪の内側(通り過ぎた跡)にも判定は残る — 広がる波としてはこれで正しい。
	transform.scale_ = {radius, 1.0f, radius};
	ApplyShockwaveFade(1.0f);
	wave->SetActive(true);
}

void GuardianBossComponent::ApplyShockwaveFade(float alphaScale) {
	if (!shockwave_) {
		return;
	}
	// 色は基準色をそのまま使い、αだけを進捗で落とす。
	Vector4 color = shockwaveBaseColor_;
	color.w *= std::clamp(alphaScale, 0.0f, 1.0f);
	if (ModelRendererComponent* renderer = shockwave_->GetComponent<ModelRendererComponent>()) {
		renderer->SetColorOverride(color);
	}
}

void GuardianBossComponent::StopShockwave() {
	shockwaveTimer_ = 0.0f;
	shockwaveHold_ = false;
	if (!shockwave_) {
		return;
	}
	if (EnemyWeapon* weapon = shockwave_->GetComponent<EnemyWeapon>()) {
		weapon->SetAttack(false);
	}
	shockwave_->SetActive(false);
}

void GuardianBossComponent::UpdateShockwave(float deltaTime) {
	if (!shockwave_ || !shockwave_->IsActive()) {
		return;
	}

	if (!shockwaveHold_) {
		shockwaveTimer_ -= deltaTime;
		if (shockwaveTimer_ <= 0.0f) {
			StopShockwave();
			return;
		}
	}

	// 経過に応じて広がる(hold中は常に終了半径のまま=一定の刃)。
	float t = 1.0f;
	if (!shockwaveHold_ && shockwaveDuration_ > 0.0f) {
		t = 1.0f - std::clamp(shockwaveTimer_ / shockwaveDuration_, 0.0f, 1.0f);
	}
	float radius = shockwaveStartRadius_ + (shockwaveEndRadius_ - shockwaveStartRadius_) * SmoothStep(t);

	WorldTransform& transform = shockwave_->GetTransform();
	if (shockwaveFollow_ && owner_) {
		// 追従する場合は高さだけ開始時の値を保つ(地面に沿って走る刃にする)。
		Vector3 position = owner_->GetTransform().translation_;
		position.y = shockwaveCenter_.y;
		transform.translation_ = position;
	}
	transform.scale_ = {radius, 1.0f, radius};

	// 広がるほど薄れて消える(土煙が散る)。出しっぱなしのコマ回転は濃さを保つ。
	ApplyShockwaveFade(shockwaveHold_ ? 1.0f : (1.0f - t));
}

void GuardianBossComponent::SetAllStrikeScales(float scale) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig) {
		return;
	}
	for (int index = 0; index < rig->GetLegCount(); ++index) {
		std::string strikeName = "Leg" + std::to_string(index) + strikeObjectSuffix_;
		if (GameObject* strike = FindDescendantByName(owner_, strikeName)) {
			// SphereColliderComponentの半径はTransformのスケール(最大成分)に追従するので、
			// スケールを変えるだけで判定の太さが変わる。見た目は空のオブジェクトなので影響しない。
			strike->GetTransform().scale_ = {scale, scale, scale};
		}
	}
}

void GuardianBossComponent::UpdatePhaseTransition() {
	if (phase2_ || phase2HealthPercent_ <= 0.0f || !health_) {
		return;
	}
	if (!health_->IsAlive()) {
		return; // 倒れた瞬間に移行させない(撃破演出と喧嘩する)。
	}
	if (health_->GetHealthPercent() <= phase2HealthPercent_) {
		EnterPhase2();
	}
}

void GuardianBossComponent::EnterPhase2() {
	phase2_ = true;

	// 常時発光へ切り替える。**ここが唯一の「見て分かる」手掛かり**なので必ず出す。
	RestoreBaseEmissive();

	// 周りを一度弾いて「変わった」と体で分からせる。ダメージは低め(合図であって攻撃ではない)。
	if (phase2BurstRadius_ > 0.0f && owner_) {
		emittingPhase2Burst_ = true;
		StartShockwave(owner_->GetTransform().translation_, 1.0f, phase2BurstRadius_, 0.55f, false, 6.0f, 12.0f, 0.4f, 0.5f);
		emittingPhase2Burst_ = false;
	}
}

void GuardianBossComponent::RestoreBaseEmissive() {
	// 予兆の点滅など、SetBodyEmissiveで直接書いた値から地の明るさへ戻す。
	// **消すのではなく基準へ戻す**のが要点で、第2形態では赤く光り続けるのが基準。
	// 実際の書き込みは毎フレームの UpdateEmissive が行うので、ここでは目標へ寄せ直すだけでよい。
	float base = phase2_ ? (std::max)(idleEmissiveIntensity_, phase2EmissiveIntensity_) : idleEmissiveIntensity_;
	currentEmissive_ = base;
	SetBodyEmissive(true, phase2_ ? phase2EmissiveColor_ : baseEmissiveColor_, base);
}

void GuardianBossComponent::SetBodyEmissive(bool active, const Vector3& color, float intensity) {
	// **光らせる先は目(見た目の本体)。**
	// 土台(Body)は接合部をぶら下げるだけの空の器でModelRendererを持たないので、
	// そちらへ書いても何も起きない(以前はここを見ていて、予兆の赤点滅が一切出ていなかった)。
	GameObject* body = nullptr;
	if (GuardianBody* guardianBody = GetComponent<GuardianBody>()) {
		body = guardianBody->GetEyeObject();
	}
	if (!body) {
		IGuardianLegRig* rig = GetRig();
		body = rig ? rig->GetBodyObject() : nullptr;
	}
	if (!body) {
		return;
	}
	ModelRendererComponent* renderer = body->GetComponent<ModelRendererComponent>();
	if (!renderer) {
		return;
	}
	if (active) {
		renderer->SetEmissiveOverride(color, intensity);
	} else {
		renderer->ClearEmissiveOverride();
	}
}

void GuardianBossComponent::SetBodyPitchOffset(float radian) {
	if (GuardianBody* body = GetComponent<GuardianBody>()) {
		body->SetPitchOffset(radian);
	}
}

void GuardianBossComponent::SetBodySink(float offset) {
	if (GuardianBody* body = GetComponent<GuardianBody>()) {
		body->SetHeightOffset(offset);
	}
}

bool GuardianBossComponent::TickPhase(const char* phaseName, float duration, float deltaTime, float& outProgress) {
	if (currentPhase_ != phaseName) {
		currentPhase_ = phaseName;
		phaseTimer_ = 0.0f;
	}

	// **「攻撃フェーズを回している」ことをここ1箇所で拾う。**
	// 各アクションに書いて回ると、新しい攻撃を足すたびに書き忘れる。
	// 数フレームぶんの猶予を持たせているのは、フェーズとフェーズの継ぎ目で消えないようにするため。
	attackGlowTimer_ = 0.25f;
	// 味方AIへ「今は踏み込むな」と伝える。後隙(*Recover)側が SetOpen で上書きする。
	Threat::SetCommitted(owner_, 0.25f);

	phaseTimer_ += deltaTime;

	float safeDuration = (std::max)(duration, 1.0e-3f);
	outProgress = std::clamp(phaseTimer_ / safeDuration, 0.0f, 1.0f);

	if (phaseTimer_ >= safeDuration) {
		// **ここで phaseTimer_ を0へ戻してはいけない。**
		// アクションの多くは TickPhase の直後に elapsed = phaseTimer_ を読み、
		// 「elapsed < windup なら溜め中」として早期returnする作りになっている。
		// 0へ戻すと、終わったフレームの elapsed が 0 になって溜めの枝へ入り、
		// 最後まで進んだのに Running を返して最初からやり直してしまう。
		// 結果、**どの技も溜めを繰り返すだけで当たる瞬間に一度も到達せず**、
		// Success が返らないので Cooldown も成立せず、Selector は永久に同じ枝を選び続ける。
		// 次にこのフェーズへ入るときに currentPhase_ の変化で 0 に戻るので、残しておいて問題ない。
		currentPhase_.clear();
		return true;
	}
	return false;
}

IGuardianLegRig* GuardianBossComponent::GetRig() { return GetComponent<IGuardianLegRig>(); }

KujataEngine::GameObject* GuardianBossComponent::FindTarget() {
	if (!owner_ || !owner_->GetScene()) {
		return nullptr;
	}

	// **ヘイト表があればそちらに従う。**
	// 最寄りを狙う方式だと、殴ってきた相手より単に近いだけの相手へ寄ってしまい、
	// キャラを切り替えて攻めても敵の狙いが変わらない。表が無い個体は従来どおり最寄りを狙う。
	if (HateTable* hate = GetComponent<HateTable>()) {
		if (GameObject* hated = hate->GetTarget()) {
			// **狙いをパーティへ申告する。** 狙われている側は避けに、そうでない側は攻めに回る。
			Threat::SetAggro(owner_, hated);
			return hated;
		}
	}

	// targetTag_の付いた生存キャラのうち最寄りを狙う(HammerEnemyComponentと同じ方針)。
	GameObject* nearest = nullptr;
	float nearestDistSq = 0.0f;
	const Vector3 selfPosition = owner_->GetTransform().translation_;
	for (const auto& object : owner_->GetScene()->GetGameObjects()) {
		if (!object || !object->IsActiveInHierarchy() || object->GetTag() != targetTag_) {
			continue;
		}
		PlayerHealth* health = object->GetComponent<PlayerHealth>();
		if (!health || !health->IsAlive()) {
			continue;
		}
		Vector3 diff = object->GetTransform().translation_ - selfPosition;
		diff.y = 0.0f;
		float distSq = diff.x * diff.x + diff.z * diff.z;
		if (!nearest || distSq < nearestDistSq) {
			nearest = object.get();
			nearestDistSq = distSq;
		}
	}
	// ヘイト表を持たない個体でも、狙いだけは同じように申告する。
	Threat::SetAggro(owner_, nearest);
	return nearest;
}

bool GuardianBossComponent::RotateTowardsTarget(float turnSpeed, float deltaTime) {
	GameObject* target = FindTarget();
	if (!owner_ || !target) {
		return false;
	}

	Vector3 toTarget = target->GetTransform().translation_ - owner_->GetTransform().translation_;
	toTarget.y = 0.0f;
	if (Length(toTarget) <= 0.0001f) {
		return true;
	}

	// +Z前方の左手系Yawはatan2(x, z)。
	float targetYaw = std::atan2(toTarget.x, toTarget.z);
	float currentYaw = owner_->GetTransform().rotation_.y;
	float difference = WrapYawAngle(targetYaw - currentYaw);

	float maxStep = turnSpeed * deltaTime;
	if (std::fabs(difference) <= maxStep) {
		owner_->GetTransform().rotation_.y = targetYaw;
		return true;
	}

	owner_->GetTransform().rotation_.y = WrapYawAngle(currentYaw + (difference > 0.0f ? maxStep : -maxStep));
	return false;
}

KujataEngine::Vector3 GuardianBossComponent::ToRootLocal(const KujataEngine::Vector3& worldPosition) const {
	return GuardianRigMath::ComputeWorldPose(GetOwner()).InverseTransformPoint(worldPosition);
}

int GuardianBossComponent::PickStompLeg(int requestedLeg) {
	IGuardianLegRig* rig = GetRig();
	if (!rig) {
		return -1;
	}
	if (requestedLeg >= 0 && requestedLeg < rig->GetLegCount()) {
		return requestedLeg;
	}

	GameObject* target = FindTarget();
	if (!target) {
		return 0;
	}

	// ルートローカルで見た対象方向と、各脚の定位置方向との内積が最大の脚を選ぶ。
	// ルート基準なので、胴体が回っていても選択がぶれない。
	Vector3 targetLocal = ToRootLocal(target->GetTransform().translation_);
	targetLocal.y = 0.0f;
	targetLocal = GuardianRigMath::SafeNormalize(targetLocal, {0.0f, 0.0f, 1.0f});

	int best = 0;
	float bestDot = -2.0f;
	for (int index = 0; index < rig->GetLegCount(); ++index) {
		Vector3 home = rig->GetHomeLocal(index);
		home.y = 0.0f;
		home = GuardianRigMath::SafeNormalize(home, {0.0f, 0.0f, 1.0f});

		float dot = GuardianRigMath::Dot3(home, targetLocal);
		if (dot > bestDot) {
			bestDot = dot;
			best = index;
		}
	}
	return best;
}

void GuardianBossComponent::SetStrikeActive(int legIndex, bool active) {
	IGuardianLegRig* rig = GetRig();
	if (!owner_ || !rig || legIndex < 0) {
		return;
	}

	// 脚の名前は "Leg0" 等。攻撃判定は "Leg0_Strike" に付いている想定。
	std::string strikeName = "Leg" + std::to_string(legIndex) + strikeObjectSuffix_;
	GameObject* strike = FindDescendantByName(owner_, strikeName);
	if (!strike) {
		return;
	}

	if (EnemyWeapon* weapon = strike->GetComponent<EnemyWeapon>()) {
		weapon->SetAttack(active);
	}
}

void GuardianBossComponent::AbortAttack() {
	IGuardianLegRig* rig = GetRig();
	if (rig) {
		if (activeLeg_ >= 0) {
			rig->SetCurveWeight(activeLeg_, 0.0f);
		}
		// 中断経路でも直線モードが残らないよう、全脚ぶん落としておく。
		// 曲線のまま歩くのが既定で、直線はジェット回転中だけの一時的な状態。
		for (int index = 0; index < rig->GetLegCount(); ++index) {
			rig->SetCurveStraight(index, false);
		}
	}
	SetAllStrikesActive(false);
	// 衝撃刃も一時的なものなので必ず消す。
	StopShockwave();
	// 判定の太らせと予兆の発光も、一時的にONにするものなので必ず戻す。
	SetAllStrikeScales(1.0f);
	RestoreBaseEmissive();
	// 中断されても照射しっぱなしにならないよう、必ず消す。
	// 「一時的にONにするものは、中断経路でも必ずOFFへ戻す」— 直線モードで一度やらかしている。
	HideBeam();
	SetBodySink(0.0f);

	// **目の分離も一時的な状態。** 中断で切り離したまま終わると、目が地面に転がったまま歩き出す。
	// ただし第2形態は「浮いたまま」が常態なので、EyeHoverが毎Tick切り離し直す。
	if (GuardianBody* body = GetComponent<GuardianBody>()) {
		body->ResetEye();
		if (GameObject* eye = body->GetEyeObject()) {
			// 溜めで縮めた/膨らませた目の大きさも必ず戻す。
			eye->GetTransform().scale_ = {1.0f, 1.0f, 1.0f};
		}
	}
	volleyIndex_ = 0;

	// **出した予告は必ず取り下げる。**
	// 攻撃が消えたのに危険域が残ると、味方AIは存在しない攻撃から永久に逃げ続ける。
	Threat::Withdraw(attackThreatId_);
	Threat::Withdraw(eyeThreatId_);
	Threat::Withdraw(eyeSnipeThreatId_);
	attackThreatId_ = 0;
	eyeThreatId_ = 0;
	eyeSnipeThreatId_ = 0;

	activeLeg_ = -1;
	currentPhase_.clear();
	phaseTimer_ = 0.0f;
}
