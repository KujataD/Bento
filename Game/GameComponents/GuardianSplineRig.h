#pragma once

#include "IGuardianLegRig.h"

#include <string>
#include <vector>

/// <summary>
/// 曲線駆動の脚1本分。関節数が可変なので、ボーンごとの長さではなく
/// 「全長 + 関節数 + 太さのテーパー」で寸法を指定する(関節を増やしても設定項目が増えない)。
/// </summary>
struct GuardianSplineLeg {
	/// <summary>
	/// 脚を構成するGameObjectの名前の接頭辞。
	/// "&lt;prefix&gt;_Hip" と "&lt;prefix&gt;_Bone0".."_Bone7"、その子の "_Bone0Mesh".."_Bone7Mesh" を階層から探す。
	/// </summary>
	std::string name_ = "Leg0";

	// --- 接合部: 球体ボディのどこから脚が生えるか ---
	float hipYawDeg_ = 45.0f;
	float hipPitchDeg_ = -10.0f;
	float hipRadius_ = 1.2f;

	// --- 寸法 ---
	// 使用する関節(ボーン)の数。階層に用意されている本数を上限にクランプされ、
	// 余ったボーンはSetActive(false)で隠れる。
	int jointCount_ = 5;
	// 付け根から足先までのボーン長の合計。そのまま脚の最大到達距離になる。
	// 低く横へ張り出す姿勢にすると水平方向を長く使うので、その到達距離を確保できる長さが要る。
	float totalLength_ = 4.2f;
	// 付け根側の太さ。
	float baseThickness_ = 0.34f;
	// 足先側の太さ。baseより細くするとテーパーの効いた機械脚になる。
	float tipThickness_ = 0.16f;

	// --- 曲線ハンドル(ここが「曲線で制御」する部分) ---
	// 付け根から脚が出ていく向き。ボディローカル空間なので、球体が傾けば一緒に傾く。
	// 2ボーンIKの kneeUp / kneeOutward に相当するが、ハンドルなので表現力が高い。
	// **ここのY成分が「どの高さで曲がるか」を決める。**
	// 正にすると曲線が接合部より上へ膨らみ、脚が高い位置で折れて縦に立つ。
	// 負にすると付け根から下外へ抜け、曲がりの頂点が接合部より低い位置に来る。
	// 既定はコンストラクタが脚ごとに「外向き1.0 + 上-0.15」で上書きする。
	KujataEngine::Vector3 hipTangent_ = {0.0f, -0.15f, 0.0f};
	// そのハンドルの長さ(付け根→足先の距離に対する倍率)。
	// footTangentScale_ より大きくすると膨らみが付け根寄りへ、小さくすると足先寄りへ移る。
	float hipTangentScale_ = 0.55f;
	// 足先へ入っていく向き。ルートローカル空間。
	// 既定はコンストラクタが脚ごとに「外向き0.9 + 上0.55」で上書きする。
	// 両ハンドルとも外を向くので曲線は上ではなく外へ膨らみ、
	// 脚が足の位置より外側へ弓なりに張り出してから内へ戻る。
	KujataEngine::Vector3 footTangent_ = {0.0f, 0.55f, 0.0f};
	float footTangentScale_ = 0.55f;

	// 曲線の弧長がtotalLength_に一致するようハンドル長を自動調整するか。
	// onにするとボーンの伸縮が数%に収まる。offだとハンドル長がそのまま効く(意図的に伸ばしたい時用)。
	bool matchArcLength_ = true;

	// --- 基準接地位置 ---
	float homeYawDeg_ = 45.0f;
	// ルート中心から足の定位置までの水平距離。大きいほど低く広く踏ん張った姿勢になる。
	float homeDistance_ = 3.4f;

	// --- 曲線レイヤーとの接続点(2ボーン版と同じ意味) ---
	// 0 = プロシージャル歩行に任せる / 1 = curveTarget_ へ完全追従。
	float curveWeight_ = 0.0f;
	// 曲線制御時の足先目標。Guardianルート基準のローカル座標。
	KujataEngine::Vector3 curveTarget_ = {1.7f, 0.0f, 1.7f};

	// --- 実行状態(シリアライズしない) ---
	KujataEngine::Vector3 proceduralTarget_ = {0.0f, 0.0f, 0.0f};
	KujataEngine::Vector3 resolvedFoot_ = {0.0f, 0.0f, 0.0f};
	// 目標が遠すぎてボーンが伸ばされた割合(1.0で無理なし)。
	float stretchRatio_ = 1.0f;
	// 直線モード。ハンドルと弧長合わせを無視し、接合部→目標を一直線に結ぶ。
	bool curveStraight_ = false;

	// --- 解決済みのGameObject ---
	KujataEngine::GameObject* hipObject_ = nullptr;
	// 階層に存在するボーンを先頭から順に。要素数が関節数の上限になる。
	std::vector<KujataEngine::GameObject*> boneObjects_;
	std::vector<KujataEngine::GameObject*> boneMeshObjects_;

	/// <summary>実際に使う関節数(階層にあるボーン数でクランプ)。</summary>
	int GetActiveJointCount() const;

	void RegisterSerializedFields(KujataEngine::SerializedFieldRegistry& registry);
};

/// <summary>
/// 曲線(3次ベジェ)駆動の脚リグ。関節数を増やしても解が破綻しない。
///
/// 2ボーンの解析IKは関節が3つ以上になると解が一意に決まらず、CCD/FABRIKのような反復解法が必要になり、
/// 収束の揺れやフレーム間のポップが出る。こちらは「先に脚の形(曲線)を決めて、そこにボーンを並べる」ので
/// 反復が無く決定的で、関節数はただのパラメータになる。
///
/// 曲線の定義:
///   P0 = 接合部(Hip)                       固定
///   P1 = P0 + hipTangent  * scale * 距離     付け根から出ていく向き … 曲線レイヤーの管轄
///   P2 = P3 + footTangent * scale * 距離     足先へ入っていく向き
///   P3 = 足先目標                            GuardianGait(歩行) と curveTarget を curveWeight でブレンド
///
/// ボーンは曲線上に等弧長で並べ、各ボーンは「隣の関節を向く剛体」として置く。
/// 曲線に沿った長さ(弧長)と関節間の直線距離(弦長)は必ずズレるので、
/// **ボーンのメッシュを実際の弦長に合わせて伸縮させる**ことで曲線にモデルを一致させる。
/// matchArcLength をonにすると曲線の弧長がtotalLengthに揃うようハンドル長を二分探索するため、
/// 伸縮は数%に収まって目視では分からない。目標が届かない距離なら素直に伸びて「脚を突っ張った」表現になる。
///
/// 前提となる階層(GuardianSpline.prefab.json が生成する形):
///   Guardian              ← GuardianGait, GuardianBody, GuardianSplineRig をこの順で持つ
///   └─ Body               ← 球体
///      └─ Leg0_Hip        ← 接合部。yaw/pitch/radiusからこのComponentが毎フレーム配置する
///         └─ Leg0_Bone0   ← 以下ボーンが親子で連なる
///            ├─ Leg0_Bone0Mesh
///            └─ Leg0_Bone1
///               └─ ...
///
/// ボーンの規約は2ボーン版と同じ: ローカル+Zがボーンの向き、チェーンのscaleは1、見た目は*Mesh子で表現。
/// </summary>
class GuardianSplineRig : public IGuardianLegRig {
public:
	/// <summary>4本の脚に既定の名前と配置角(前左/前右/後右/後左)を入れます。</summary>
	GuardianSplineRig();

	const char* GetTypeName() const override { return "GuardianSplineRig"; }
	bool AllowMultiple() const override { return false; }

	void Initialize() override;
	void OnPlayStart() override;
	void Update() override;

	// --- IGuardianLegRig ---
	/// <summary>
	/// 実際に使う脚の本数。**階層に用意されている本数より少なくてよい。**
	/// 余った脚はUpdateで丸ごと非表示にするので、4本ぶんのPrefabのまま3脚にできる。
	/// </summary>
	int GetLegCount() const override {
		if (legCount_ < 1) {
			return 1;
		}
		return (legCount_ > kGuardianLegSlotCount) ? kGuardianLegSlotCount : legCount_;
	}
	KujataEngine::Vector3 GetHipWorld(int index) const override;
	float GetMaxReach(int index) const override;
	KujataEngine::Vector3 GetHomeWorld(int index) const override;
	void SetProceduralTarget(int index, const KujataEngine::Vector3& worldPosition) override;
	KujataEngine::Vector3 GetProceduralTarget(int index) const override;
	KujataEngine::Vector3 GetFootWorld(int index) const override;
	bool IsCurveDriven(int index) const override;
	void SetCurveWeight(int index, float weight) override;
	float GetCurveWeight(int index) const override;
	void SetCurveTargetLocal(int index, const KujataEngine::Vector3& rootLocalPosition) override;
	KujataEngine::Vector3 GetCurveTargetLocal(int index) const override;
	void SetCurveStraight(int index, bool straight) override;
	bool IsCurveStraight(int index) const override;
	KujataEngine::Vector3 GetHomeLocal(int index) const override;
	KujataEngine::GameObject* GetBodyObject() const override { return bodyObject_; }

	GuardianSplineLeg* GetLeg(int index);
	const GuardianSplineLeg* GetLeg(int index) const;

	/// <summary>階層の解決をやり直します。子オブジェクトを差し替えた後に呼びます。</summary>
	void ResolveHierarchy();

	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;

private:
	/// <summary>接合部オブジェクトを yaw/pitch/radius からボディローカルへ配置します。</summary>
	void ApplyHipPlacement(GuardianSplineLeg& leg);

	/// <summary>曲線を組み立ててボーンを並べます。</summary>
	void SolveLeg(GuardianSplineLeg& leg);

	/// <summary>使わないボーンをSetActive(false)で隠します(チェーンなので以降が丸ごと消える)。</summary>
	void ApplyJointVisibility(GuardianSplineLeg& leg);

public:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(bodyObjectName_, "Body Object",
		    "球体ボディにあたるGameObjectの名前。接合部(Leg*_Hip)はこの子として配置される。\n"
		    "見つからない場合はルート自身を使う。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(applyHipPlacement_, "Apply Hip Placement",
		    "接合部を Hip Yaw / Pitch / Radius から毎フレーム配置する。\n"
		    "offにすると Leg*_Hip のTransformを直接編集した値がそのまま使われる。");
		KUJATA_REGISTER_INT_NAMED_TIP(curveSampleCount_, "Curve Samples", 1.0f, 8, 128,
		    "曲線を折れ線に落とすときの分割数。多いほど弧長と関節位置が正確になるが重くなる。\n"
		    "弧長合わせの二分探索でこの回数ぶん評価するので、負荷はこの値に比例する。");
		KUJATA_REGISTER_INT_NAMED_TIP(meshUpAxis_, "Mesh Up Axis", 1.0f, 0, 1,
		    "ボーンに割り当てたモデルの**長手方向**。0=Z(既定), 1=Y。\n"
		    "ボーン自体は常に+Zへ伸びるが、モデルがY方向に長く作られていることは多い。\n"
		    "**寝てしまう・潰れる場合はここを切り替える**(モデルを書き出し直す必要はない)。");
		KUJATA_REGISTER_INT_NAMED_TIP(legCount_, "Leg Count", 1.0f, 1, kGuardianLegSlotCount,
		    "実際に使う脚の本数。ここを減らすと余った脚は丸ごと隠れるので、\n"
		    "4本ぶんの階層を持つPrefabのまま3脚・2脚にできる。\n"
		    "**減らしたら残る脚のYaw(Hip Yaw / Home Yaw)を配り直すこと。** 偏ったまま歩くと転んだように見える。");
		KUJATA_REGISTER_OBJECT_NAMED_TIP(leg0_, "Leg 0 (Front Left)",
		    "前左の脚。歩容では Leg2(後右)と同じグループで、対角ペアとして同時に踏み出す。");
		KUJATA_REGISTER_OBJECT_NAMED_TIP(leg1_, "Leg 1 (Front Right)",
		    "前右の脚。歩容では Leg3(後左)と同じグループ。");
		KUJATA_REGISTER_OBJECT_NAMED_TIP(leg2_, "Leg 2 (Back Right)",
		    "後右の脚。歩容では Leg0(前左)と同じグループ。");
		KUJATA_REGISTER_OBJECT_NAMED_TIP(leg3_, "Leg 3 (Back Left)",
		    "後左の脚。歩容では Leg1(前右)と同じグループ。");
	}

private:
	std::string bodyObjectName_ = "Body";
	bool applyHipPlacement_ = true;

	// 曲線を折れ線に落とすときの分割数。多いほど弧長と関節位置が正確になる。
	int curveSampleCount_ = 32;

	// 使う脚の本数(1〜4)。階層は4本ぶん用意しておき、ここで使う数だけ絞る。
	KUJATA_FIELD_INT(legCount_, kGuardianLegSlotCount);
	// ボーンメッシュの長手方向(0=Z, 1=Y)。モデルの作りに合わせる。
	KUJATA_FIELD_INT(meshUpAxis_, 0);

	GuardianSplineLeg leg0_{};
	GuardianSplineLeg leg1_{};
	GuardianSplineLeg leg2_{};
	GuardianSplineLeg leg3_{};

	KujataEngine::GameObject* bodyObject_ = nullptr;
	bool hierarchyResolved_ = false;
};
