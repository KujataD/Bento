#pragma once

#include "IGuardianLegRig.h"

/// <summary>
/// ガーディアンのプロシージャル歩行。「足先をどこへ置くか」だけを決めて脚リグへ渡す。
/// 関節をどう曲げるかはRig側の仕事なので、この2つは独立に差し替えられる。
///
/// 歩き方の考え方(いわゆる procedural foot planting):
///   - 各脚には定位置(home)がある。ボスが動くとhomeも動くが、足はその場に接地したまま残る
///   - 足と定位置のズレが stepThreshold を超えたら、その脚だけを弧を描いて踏み出す
///   - 踏み出し先は「今のhome + 進行方向へ stepLead 秒ぶん先読み」した位置。歩幅が速度に追従する
/// これによりアニメーションクリップを1つも作らずに、任意の速度・任意の方向の歩行が成立する。
///
/// 脚には最大長があるので、足を地面に貼り付け続けることはできない。接地点までの距離が
/// 「最大長 × detachReachRatio」を超えたら足を地面から離し、接合部からぶら下げる。
/// これでジャンプ・落下・段差からの踏み外しが破綻せずに扱える(足が地面に取り残されて
/// 脚が不自然に引き伸ばされるのを防ぐ)。着地は「最大長 × landReachRatio」まで地面が近づいた時で、
/// 離脱としきい値を分けてヒステリシスを作ってある。
///
/// 曲線レイヤーが脚を奪っている間(IGuardianLegRig::IsCurveDriven)は歩行を止め、
/// 解放された瞬間にその脚を踏み直させる。攻撃で振り上げた脚が着地後に自然に歩行へ復帰する。
/// </summary>
class GuardianGait : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "GuardianGait"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	/// <summary>
	/// 地面を踏んでいる脚か(踏み出し中でも空中でもない)。GuardianBodyが胴体の高さを決めるのに使う。
	/// </summary>
	bool IsPlanted(int index) const;

	/// <summary>脚が届かなくなって地面から離れているか。</summary>
	bool IsAirborne(int index) const;

	/// <summary>1本でも接地していれば true。全脚が浮いていればジャンプ/落下中。</summary>
	bool HasGroundContact() const;

	/// <summary>
	/// 指定位置の真下にある地面の高さ。着地点の高さを知りたい場合などに外から使う。
	/// 設定(Raycast Ground / Static Ground Only / レイヤーマスク)は歩行と共通。
	/// </summary>
	float SampleGroundAt(const KujataEngine::Vector3& position) const { return SampleGroundHeight(position); }

	/// <summary>0〜1で表した歩行の忙しさ。どれか1本でも踏み出していれば1に近づく。</summary>
	float GetStepActivity() const;

	/// <summary>ボスの水平移動速度[unit/s]。GuardianBodyのボビング量に使う。</summary>
	float GetPlanarSpeed() const { return planarSpeed_; }

private:
	struct LegState {
		// 接地している足先のワールド位置。踏み出し中は移動しない。
		KujataEngine::Vector3 planted = {0.0f, 0.0f, 0.0f};
		// 踏み出し中か。
		bool stepping = false;
		// 踏み出しの経過時間[s]。
		float timer = 0.0f;
		// この1歩にかける時間[s]。速度から踏み出し開始時に決めるので、脚ごとに違いうる。
		float duration = 0.28f;
		KujataEngine::Vector3 stepFrom = {0.0f, 0.0f, 0.0f};
		KujataEngine::Vector3 stepTo = {0.0f, 0.0f, 0.0f};
		// 曲線制御から解放された直後など、ズレに関係なく踏み直したい場合に立てる。
		bool needsReplant = false;
		// 脚が届かなくなって地面から離れているか(ジャンプ中・落下中・段差から踏み外した時)。
		bool airborne = false;
		// 空中で足を垂らしている位置。接地位置から滑らかに移るために保持する。
		KujataEngine::Vector3 airPosition = {0.0f, 0.0f, 0.0f};
	};

	/// <summary>同じGameObjectの脚リグ。2ボーン版でも曲線版でも同じように扱える。</summary>
	IGuardianLegRig* GetRig();

	/// <summary>全脚の接地位置を定位置へ揃えます(Play開始時)。</summary>
	void ResetToHome();

	/// <summary>
	/// 交互歩容の制約。対角のペア({0,2} と {1,3})が同時に浮かないようにして、
	/// 常に3点以上で体を支える。緊急の踏み直しはこの制約を無視する。
	/// </summary>
	/// <summary>リグが実際に使っている脚の本数(1〜4)。</summary>
	int LegCount() const;

	bool CanStartStep(int index) const;

	/// <summary>
	/// 現在の移動速度から1歩にかける時間を決めます。
	/// strideLength_ ぶん進む時間を [minStepDuration_, stepDuration_] でクランプしたもの。
	/// </summary>
	float ComputeStepDuration() const;

	/// <summary>
	/// 指定位置の真下にある地面の高さを返します。Colliderに当たらなければ groundY_ を返します。
	/// 自分(ガーディアン)の階層にあるColliderは無視します。
	/// </summary>
	float SampleGroundHeight(const KujataEngine::Vector3& position) const;

	/// <summary>
	/// 空中で脚を垂らす位置。接合部から下向き(+外向きにdangleSpread_ぶん開いた向き)へ、
	/// 最大長のdangleReachRatio_ぶん伸ばした点を返します。
	/// </summary>
	/// <summary>
	/// 待機中の微振動。**接地している足だけに乗せる見た目専用のオフセット**で、
	/// state.plantedには決して書き込まない(書き込むと踏み出し判定が誤爆し、少しずつ足が流れていく)。
	/// 脚ごとにノイズのレーンをずらしてあるので、4本が別々の位相で揺れる。
	/// </summary>
	KujataEngine::Vector3 ComputeIdleJitter(int index) const;

	KujataEngine::Vector3 ComputeDanglePosition(
	    const KujataEngine::Vector3& hipPosition, const KujataEngine::Vector3& rootPosition, float maxReach) const;

public:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stepThreshold_, "Step Threshold", 0.01f, 0.05f, 20.0f,
		    "足が定位置からこれだけ水平にズレたら踏み出す。\n大きいほど大股でのっしり歩く。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stepDuration_, "Step Duration (max)", 0.005f, 0.02f, 3.0f,
		    "低速時の1歩にかける時間[秒]。速度が上がるとここから短くなっていく。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(minStepDuration_, "Step Duration (min)", 0.005f, 0.01f, 3.0f,
		    "高速時の1歩の時間[秒]の下限。これ以上は脚の回転が上がらない。\n速く走らせても足が追いつかない場合はここを下げる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(strideLength_, "Stride Length", 0.01f, 0.05f, 30.0f,
		    "「この距離を進む時間」を1歩の時間にする。歩調(1秒あたりの歩数)を決める。\n"
		    "小さくすると細かく速く、大きくすると大股でゆっくりになる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stepHeight_, "Step Height", 0.01f, 0.0f, 10.0f,
		    "踏み出し中に足を持ち上げる高さ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stepLeadFactor_, "Step Lead Factor", 0.01f, 0.0f, 3.0f,
		    "着地点を進行方向へ先読みする量(1歩の時間に対する倍率)。\n"
		    "1.0で「着地する瞬間に定位置が来ている場所」へちょうど足を置く。\n"
		    "下げると足が後ろ寄りに着地する。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(urgentReachRatio_, "Urgent Reach Ratio", 0.005f, 0.1f, 1.0f,
		    "接地点までの距離が「脚の最大長×この値」を超えたら、\n"
		    "交互歩容の順番を無視してでも踏み直す。\n"
		    "※通常の踏み出し < この値 < Detach Reach Ratio の順に並べること。\n"
		    "  通常側に近すぎると毎回こちらが発火して交互歩容が無効になる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(footClearance_, "Foot Clearance", 0.005f, -2.0f, 5.0f,
		    "接地面から足の中心をどれだけ浮かせるか。足パーツの半径ぶん入れる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(footDustStrength_, "Foot Dust Strength", 0.01f, 0.0f, 3.0f,
		    "足が接地するたびに上げる土埃の強さ。0で出さない。\n"
		    "**攻撃の土埃(0.5〜3)よりずっと弱くすること。** 歩くたびに濃い煙が出ると画面が埋まる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(idleJitterAmplitude_, "Idle Jitter Amplitude", 0.005f, 0.0f, 1.0f,
		    "立ち止まっている時に足先を揺らす量。**完全静止だと巨体が置物に見える**ので、\n"
		    "わずかに軋ませて生き物らしさを出す。0で無効。Step Thresholdより十分小さくすること。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(idleJitterFrequency_, "Idle Jitter Frequency", 0.01f, 0.01f, 5.0f,
		    "足先の揺れの速さ[Hz相当]。上げるほど小刻みになる。0.2〜0.5あたりが「重い」印象になる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(idleJitterFadeSpeed_, "Idle Jitter Fade Speed", 0.05f, 0.05f, 20.0f,
		    "この速度[unit/s]まで歩くと揺れが完全に消える。\n歩行中は歩容そのものが揺れを持つので、待機時だけに効かせる。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(alternateGait_, "Alternate Gait",
		    "対角ペア({前左,後右} と {前右,後左})を交互に動かし、常に3点以上で体を支える。\n"
		    "offにすると各脚が独立に踏み出す(不安定だが機械的な印象になる)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(detachReachRatio_, "Detach Reach Ratio", 0.005f, 0.1f, 1.0f,
		    "接地点までの距離が「脚の最大長×この値」を超えたら足を地面から離す。\n"
		    "ジャンプや落下で脚が伸びきるのを防ぐ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(landReachRatio_, "Land Reach Ratio", 0.005f, 0.1f, 1.0f,
		    "地面が「脚の最大長×この値」まで近づいたら着地する。\n"
		    "※Detach Reach Ratio より小さくすること。同値だと境界で接地/離脱がばたつく。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dangleReachRatio_, "Dangle Reach Ratio", 0.005f, 0.1f, 1.0f,
		    "空中で脚をどれだけ伸ばして垂らすか(脚の最大長に対する割合)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dangleSpread_, "Dangle Spread", 0.005f, 0.0f, 3.0f,
		    "空中で垂らす向きを真下からどれだけ外側へ開くか。0で真下。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dangleSmoothing_, "Dangle Smoothing", 0.05f, 0.0f, 60.0f,
		    "空中での足先の追従の速さ[1/秒]。小さいほどゆっくり垂れる。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(raycastGround_, "Raycast Ground",
		    "地面をレイキャストで探す。offにすると常に Ground Y の平面に接地する。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(staticGroundOnly_, "Static Ground Only",
		    "動かないコライダーだけを足場とみなす。\n"
		    "判定はエンジンと同じ規約で「Rigidbodyを持ち、かつ Is Static でない」ものを動的として除外する。\n"
		    "offにするとプレイヤーや敵の上にも足を置き、相手が動いた瞬間に足が取り残される。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(groundY_, "Ground Y (fallback)", 0.01f, -100.0f, 100.0f,
		    "レイが何にも当たらなかった時に使う地面の高さ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(rayUp_, "Ray Up", 0.01f, 0.0f, 100.0f,
		    "足の想定位置からどれだけ上にレイの原点を置くか。\n"
		    "段差を登るときはこれより低い段しか拾えない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(rayDown_, "Ray Down", 0.01f, 0.0f, 100.0f,
		    "レイの原点から下へ何ユニットまで地面を探すか。");
		KUJATA_REGISTER_UINT32_NAMED_TIP(groundLayerMask_, "Ground Layer Mask", 1.0f, 0u, 0xffffffffu,
		    "地面とみなすレイヤーのビットマスク。");
	}

private:
	// 足が定位置からこれだけ(水平距離)離れたら踏み出す。大きいほど大股でのっしり歩く。
	float stepThreshold_ = 0.7f;

	// --- 1歩にかける時間。速度に応じて可変 ---
	// 停止時〜低速時の1歩の時間[s](上限)。
	float stepDuration_ = 0.28f;
	// 高速時の1歩の時間[s](下限)。これ以上は速くならない。
	float minStepDuration_ = 0.09f;
	// 「この距離ぶん進むのにかかる時間」を1歩の時間にする。速いほど1歩が短くなり回転が上がる。
	// 固定時間のままだと、速度が上がったとき1歩の間に胴体が進みすぎて足が永久に置き去りになる。
	float strideLength_ = 1.4f;

	// 踏み出し中に足を持ち上げる高さ。
	float stepHeight_ = 0.8f;

	// 着地点を進行方向へ先読みする量。1歩の時間に対する倍率で、1.0で
	// 「着地する瞬間に定位置が来ている場所」へちょうど足を置く。
	// 秒数で持つと1歩の時間と噛み合わず、速度が上がるほど足が後ろに着地してしまう。
	float stepLeadFactor_ = 1.0f;

	// 接地点までの距離が「最大長 × この値」を超えたら、歩容の交互制約を無視してでも踏み直す。
	//
	// 値は「通常の踏み出しが発火する距離」と detachReachRatio_ の間に置くこと。
	// 通常側に近すぎると、歩容の順番待ちが起きた瞬間に毎回こちらが発火して交互歩容が無効化される。
	// 既定値の脚(最大長3.7・定位置で74%使用)では stepThreshold_ 0.7 ぶん引きずられた時点が
	// 約86%なので、0.92 にして通常の踏み出しに6%ぶんの猶予を与えてある。
	// 脚の寸法を変えたらこの順序が保たれているか確かめること。
	float urgentReachRatio_ = 0.92f;
	// 接地位置から足の中心をどれだけ浮かせるか(足パーツの半径ぶん)。
	float footClearance_ = 0.15f;

	// 対角ペアを交互に動かすか。offにすると各脚が独立に踏み出す(不安定だが機械的な印象になる)。
	bool alternateGait_ = true;

	// --- 接地からの離脱(ジャンプ・落下・段差からの踏み外し) ---
	// 接地点までの距離が「脚の最大長×この値」を超えたら足を地面から離す。
	float detachReachRatio_ = 0.95f;
	// 逆に、地面が「脚の最大長×この値」まで近づいたら着地する。
	// detachより小さくしてヒステリシスを作る。同値にすると境界で接地/離脱がばたつく。
	float landReachRatio_ = 0.85f;
	// 空中で脚をどれだけ伸ばして垂らすか(脚の最大長に対する割合)。
	float dangleReachRatio_ = 0.8f;
	// 垂らす向きを真下からどれだけ外側へ開くか。0で真下、大きいほど脚が広がる。
	float dangleSpread_ = 0.35f;
	// 空中での足先の追従の速さ[1/s]。小さいほど脚がゆっくり垂れる。
	float dangleSmoothing_ = 9.0f;

	// 地面をレイキャストで探すか。offなら常に groundY_ の平面に接地する。
	bool raycastGround_ = true;

	// 動かないコライダーだけを足場とみなすか。
	// エンジンと同じ規約で「Rigidbodyを持ち、かつIs Staticでない」ものを動くコライダーとして除外する。
	// offにするとプレイヤーや敵の上にも足を置いてしまい、相手が動いた瞬間に足が取り残される。
	bool staticGroundOnly_ = true;
	// レイが何にも当たらなかった時の地面の高さ。
	float groundY_ = 0.0f;
	// 足の想定位置からどれだけ上からレイを撃つか。
	float rayUp_ = 4.0f;
	// そこから下へ何ユニット探すか。
	float rayDown_ = 8.0f;
	// 地面とみなすレイヤーのビットマスク。
	uint32_t groundLayerMask_ = 0xffffffffu;

	// --- 待機中の微振動(パーリンノイズ) ---
	// 見た目専用のオフセットなので、歩行の判定には一切入らない。
	float idleJitterAmplitude_ = 0.06f;
	float idleJitterFrequency_ = 0.35f;
	float idleJitterFadeSpeed_ = 1.0f;
	// ノイズに入れる時刻[s]。Play開始からの経過を自分で積む(整数に張り付かせないため0以外から始める)。
	float noiseTime_ = 0.0f;

	// 着地のたびに上げる土埃の強さ。0で無効。
	KUJATA_FIELD_FLOAT(footDustStrength_, 0.35f);

	LegState legStates_[kGuardianLegCount]{};

	// 移動速度の算出用。
	KujataEngine::Vector3 previousRootPosition_ = {0.0f, 0.0f, 0.0f};
	KujataEngine::Vector3 rootVelocity_ = {0.0f, 0.0f, 0.0f};
	float planarSpeed_ = 0.0f;
	bool hasPreviousPosition_ = false;
};
