#pragma once

#include <KujataEngine.h>
#include <string>

class GuardianGait;
class IGuardianLegRig;

/// <summary>
/// ガーディアンの球体ボディ。脚とは独立に動く「上物」を担当する。
///
/// 脚が地面に合わせて勝手に動くのに対して、ボディは接地している足の位置から
/// 「どのくらいの高さで、どちらへ傾いて浮かんでいるべきか」を毎フレーム決める。
/// 接合部(Leg*_Hip)はこのボディの子なので、ボディが沈めば脚も自動的に畳まれる。
///
/// 接地していない脚(踏み出し中・攻撃で曲線制御中)は平均から除外する。
/// 攻撃で持ち上げた脚に釣られてボディが浮き上がらないようにするため。
///
/// ボディのYaw回転は歩行と完全に独立している。spinSpeedDeg を入れると、
/// 脚がその場に接地したまま球体だけが回り続ける(ガーディアンらしい挙動)。
/// </summary>
class GuardianBody : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "GuardianBody"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	/// <summary>
	/// 胴体のYawへ回転を足します。単位はradian。
	///
	/// Updateがrotation_を毎フレーム丸ごと書き直すので、外から直接Transformを触っても
	/// 次のフレームで消える。演出や攻撃から胴体を回したい場合はここを通すこと。
	/// </summary>
	void AddSpin(float radians) { currentSpin_ += radians; }

	/// <summary>
	/// 胴体の高さへ加算するオフセット[unit]。負で沈み込む。
	/// 溜めやジャンプの予備動作など、一時的に車高を変えたいときに使う。
	/// Ride Height を直接いじると平滑化を通って戻りが鈍いので、こちらは即座に効く。
	/// </summary>
	void SetHeightOffset(float offset) { heightOffset_ = offset; }
	float GetHeightOffset() const { return heightOffset_; }

	/// <summary>
	/// 足から求めた傾きへ足す追加のピッチ[rad]。**致命を受けた大きな仰け反り**のように、
	/// 地形とは無関係に胴体を反らせたいときに使う。0で通常。
	/// </summary>
	void SetPitchOffset(float radian) { pitchOffset_ = radian; }
	float GetPitchOffset() const { return pitchOffset_; }

	// --- 被弾のリアクション(加算レイヤー) ---
	//
	// **攻撃モーションを止めずに、その上へ混ぜるためのチャンネル。**
	// 以前は被弾で攻撃を中断していたが、それだと殴られ続ける限り技が振り出しに戻り、
	// 当たる瞬間に一度も到達しない。リアクションは見た目だけの層に分けて、
	// 行動の中断は体勢崩し(スタン)だけが行う、という切り分けにしてある。
	//
	// 毎フレーム**絶対値で**書く前提(足し込まない)。書くのをやめれば自然に消える。

	/// <summary>
	/// のけぞりのポーズを差し込む。offsetは目に足す位置
	/// (目が親子なら親基準ローカル、分離中ならワールド)、sinkは沈み込み、
	/// pitch/rollは殴られた向きへの傾き[rad]。
	/// </summary>
	void SetReactionPose(const KujataEngine::Vector3& offset, float sink, float pitch, float roll) {
		reactionOffset_ = offset;
		reactionSink_ = sink;
		reactionPitch_ = pitch;
		reactionRoll_ = roll;
	}

	/// <summary>のけぞりを畳む(中断経路から必ず通すこと)。</summary>
	void ClearReactionPose() { SetReactionPose({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f); }

	// --- 目(Eye) ---
	//
	// **目は脚と完全に分離している。** 接合部をぶら下げている土台(Body)と、
	// 見た目の本体である目は別のオブジェクトで、目は土台の高さも傾きも受け継がない。
	// こうしておくと「目だけ地面に降ろして脚を宙に浮かせる」「目だけ回してビームを薙ぐ」が、
	// 歩行にも接合部にも一切影響を与えずに書ける。

	/// <summary>目のGameObject(見つからなければnullptr)。</summary>
	KujataEngine::GameObject* GetEyeObject() const;

	/// <summary>
	/// 目が親子関係の外にあるか(第2形態はこちら)。
	/// trueなら SetEyeOffset は**ワールド座標**として扱われる。
	/// </summary>
	bool IsEyeDetachedObject() const;

	/// <summary>
	/// 目を土台から切り離す。trueの間、目の高さは土台の車高を無視して
	/// SetEyeOffset の y をそのままローカル高さとして使う(分離攻撃用)。
	/// </summary>
	void SetEyeDetached(bool detached) { eyeDetached_ = detached; }
	bool IsEyeDetached() const { return eyeDetached_; }

	/// <summary>目の位置オフセット(ルート基準ローカル)。分離中は y がそのまま高さになる。</summary>
	void SetEyeOffset(const KujataEngine::Vector3& offset) { eyeOffset_ = offset; }
	const KujataEngine::Vector3& GetEyeOffset() const { return eyeOffset_; }

	/// <summary>目の追加ピッチ[rad]。ビームを真上へ向けたり振り下ろしたりするのに使う。</summary>
	void SetEyePitch(float radian) { eyePitch_ = radian; }
	float GetEyePitch() const { return eyePitch_; }

	/// <summary>目の独立Yaw[rad]。胴体の回転(spinSpeed)とは別に足される。</summary>
	void SetEyeYaw(float radian) { eyeYaw_ = radian; }

	/// <summary>
	/// **目のモデルの向き合わせ[度]。** 見た目だけに効く補正で、狙い(ビームの向き)には影響しない。
	/// 第1形態の目はルートの子なのでルートの向きを継ぐが、第2形態の目は完全に分離していて
	/// ルートの向きを一切継がない。そのぶん**2つの形態で目の正面が180度ずれていた**ので、
	/// 分離中だけこの角度を足して揃える。
	/// </summary>
	void SetDetachedEyeYawOffsetDeg(float degrees) { detachedEyeYawOffsetDeg_ = degrees; }
	void AddEyeYaw(float radians) { eyeYaw_ += radians; }
	float GetEyeYaw() const { return eyeYaw_; }

	/// <summary>目に関する一時状態をすべて既定へ戻す(攻撃の中断経路から必ず通すこと)。</summary>
	void ResetEye() {
		eyeDetached_ = false;
		eyeOffset_ = {0.0f, 0.0f, 0.0f};
		eyePitch_ = 0.0f;
		eyeYaw_ = 0.0f;
	}

private:
	/// <summary>接地中の脚だけを集めて、平均の高さと前後左右の高低差を求めます。</summary>
	bool GatherPlantedFeet(
	    IGuardianLegRig& rig, const GuardianGait* gait, float& outAverageHeight, float& outPitchSlope, float& outRollSlope) const;

public:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT_NAMED_TIP(rideHeight_, "Ride Height", 0.01f, 0.0f, 30.0f,
		    "接地している足の平均高さから、ボディ中心をどれだけ上に置くか。いわば車高。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(maxSuspensionTravel_, "Max Suspension Travel", 0.01f, 0.0f, 30.0f,
		    "足の高さへの追従で、ボディが Ride Height からずれてよい最大量(サスペンションのストローク)。\n"
		    "**これが無いとジャンプできない。** ルートのYが完全に打ち消され、\n"
		    "ボディのワールド高さが「足の高さ + Ride Height」に固定されてしまうため。\n"
		    "ストロークを超える段差には追従しきれないので、地形の起伏より少し大きめに取る。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(heightSmoothing_, "Height Smoothing", 0.05f, 0.0f, 60.0f,
		    "高さ追従の速さ[1/秒]。大きいほど地形に機敏に反応する。0で追従しない。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(alignToFeet_, "Align To Feet",
		    "接地している足の高低差に合わせてボディを傾ける。\n"
		    "踏み出し中や曲線制御中の脚は平均から除外されるので、\n"
		    "攻撃で持ち上げた脚に釣られて傾いたりはしない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(tiltStrength_, "Tilt Strength", 0.01f, -5.0f, 5.0f,
		    "傾きの強さ。**符号を反転すると傾く向きが逆になる。**\n"
		    "モデルの前後の向きに合わせて調整する。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(maxTiltDeg_, "Max Tilt (deg)", 0.1f, 0.0f, 89.0f,
		    "傾きの上限。急な段差で極端に傾くのを防ぐ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(tiltSmoothing_, "Tilt Smoothing", 0.05f, 0.0f, 60.0f,
		    "傾き追従の速さ[1/秒]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(bobAmplitude_, "Bob Amplitude", 0.005f, 0.0f, 5.0f,
		    "歩行中の上下の揺れ幅。移動速度に比例するので、止まれば揺れない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(bobFrequency_, "Bob Frequency", 0.01f, 0.0f, 20.0f,
		    "揺れの速さ[Hz相当]。歩調とは連動していないので、合わないときはここで合わせる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(bobSpeedReference_, "Bob Speed Reference", 0.01f, 0.01f, 50.0f,
		    "この移動速度[unit/秒]で揺れ幅が最大になる。これ以上速くても揺れは増えない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(idleSwayAmplitude_, "Idle Sway Amplitude", 0.005f, 0.0f, 2.0f,
		    "立ち止まっている時に胴体を上下させる量。歩行中のBobとは別物で、\n"
		    "**止まっている間に効く**。0で無効。Bob Amplitudeの半分くらいが目安。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(idleSwayFrequency_, "Idle Sway Frequency", 0.01f, 0.01f, 5.0f,
		    "胴体の揺れの速さ[Hz相当]。脚(Idle Jitter Frequency)とは別の値にしておくと、\n"
		    "周期が噛み合わずに自然な軋みになる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(spinSpeedDeg_, "Spin Speed (deg/s)", 0.5f, -720.0f, 720.0f,
		    "ボディだけを回し続ける速度。**脚は接地したまま球体だけが回る。**\n"
		    "足の定位置がルート基準なので、ボディの回転に脚は影響されない。");
		KUJATA_REGISTER_STRING_NAMED_TIP(eyeObjectName_, "Eye Object",
		    "見た目の本体(目)になる、ルート直下の子オブジェクト名。\n"
		    "**脚をぶら下げている土台(Body)とは別物**で、土台の高さも傾きも受け継がない。\n"
		    "見つからない場合は旧名 \"BodyMesh\" も探す。");
	}

private:
	// 接地している足の平均高さから、ボディ中心をどれだけ上に置くか。
	// 低くするほど脚が横へ寝て、踏ん張った姿勢になる。
	float rideHeight_ = 1.7f;

	// 足の高さへの追従で、ボディが rideHeight_ からずれてよい最大量(サスペンションのストローク)。
	// これが無いとルートのYが完全に打ち消され、ルートを上げてもボディのワールド高さが
	// 「足の高さ + rideHeight_」に固定される。結果として接合部が上がらず脚が伸びきらないので、
	// GuardianGait の離脱判定が発火せずジャンプにならない。
	// このストロークを超える段差には追従しきれなくなるので、地形の起伏より少し大きめに取る。
	float maxSuspensionTravel_ = 0.6f;
	// 高さ追従の速さ[1/s]。大きいほど地形に機敏に反応する。0で追従しない。
	float heightSmoothing_ = 8.0f;

	// 足の高低差からボディを傾けるか。
	bool alignToFeet_ = true;
	// 傾きの強さ。符号を反転すると傾く向きが逆になる(モデルの向きに合わせて調整する)。
	float tiltStrength_ = 1.0f;
	// 傾きの上限[deg]。
	float maxTiltDeg_ = 20.0f;
	// 傾き追従の速さ[1/s]。
	float tiltSmoothing_ = 6.0f;

	// 歩行中の上下の揺れ幅。
	float bobAmplitude_ = 0.12f;
	// 揺れの速さ[Hz相当]。
	float bobFrequency_ = 2.4f;
	// この速度[unit/s]で揺れ幅が最大になる。
	float bobSpeedReference_ = 4.0f;

	// --- 待機中の揺れ(パーリンノイズ) ---
	// 歩行中のBobは正弦波だが、こちらはノイズ。周期が読めないので「生きている」感じになる。
	float idleSwayAmplitude_ = 0.05f;
	float idleSwayFrequency_ = 0.22f;

	// ボディだけを回し続ける速度[deg/s]。脚は接地したまま球体が回る。
	float spinSpeedDeg_ = 0.0f;

	// 目(見た目の本体)のオブジェクト名。旧プレハブ互換のため "BodyMesh" も探す。
	std::string eyeObjectName_ = "Eye";

	// --- 実行状態 ---
	// 平滑後のボディのローカル高さ。
	float currentHeight_ = 0.0f;
	// 平滑後の傾き[rad]。
	float currentPitch_ = 0.0f;
	float currentRoll_ = 0.0f;
	// ボディの独立Yaw[rad]。
	float currentSpin_ = 0.0f;
	// ボビングの位相[rad]。
	float bobPhase_ = 0.0f;
	// ノイズに入れる時刻[s]。
	float noiseTime_ = 0.0f;
	// 外部から与える高さオフセット(平滑化を通さず即座に効く)。
	float heightOffset_ = 0.0f;
	// 外部から与える追加ピッチ[rad](致命の仰け反りなど)。平滑化を通さず即座に効く。
	float pitchOffset_ = 0.0f;
	// 被弾リアクション(加算レイヤー)。GuardianBossComponent が毎フレーム絶対値で書く。
	KujataEngine::Vector3 reactionOffset_ = {0.0f, 0.0f, 0.0f};
	float reactionSink_ = 0.0f;
	float reactionPitch_ = 0.0f;
	float reactionRoll_ = 0.0f;
	// 分離中の目にだけ足す向きの補正[度]。既定180 = 第1形態と正面を揃える。
	float detachedEyeYawOffsetDeg_ = 180.0f;

	// --- 目の独立状態 ---
	// 土台の車高から切り離されているか(分離攻撃中)。
	bool eyeDetached_ = false;
	// 目の位置オフセット(ルート基準ローカル)。分離中は y がそのまま高さ。
	KujataEngine::Vector3 eyeOffset_ = {0.0f, 0.0f, 0.0f};
	// 目の追加ピッチ/Yaw[rad]。
	float eyePitch_ = 0.0f;
	float eyeYaw_ = 0.0f;
	// 初回フレームは平滑せず即座に合わせる。
	bool initialized_ = false;
};
