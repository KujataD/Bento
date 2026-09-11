#pragma once
#include <KujataEngine.h>
#include <string>
#include <vector>

class IEnemy;

/// <summary>
/// Z注目(フロム式ロックオン)。PartyManagerと同じGameObjectに置くシーン常駐Component。
///
///   R3 / Q            : 注目ON/OFF(ONのときは画面中央に近い敵を選ぶ)
///   右スティック左右   : 注目中に「倒した瞬間」だけ隣の敵へ切替(倒しっぱなしでは連続しない)
///   自動解除           : 対象の死亡・非アクティブ・Release Distance超え
///
/// 注目中は毎フレーム
///   - OrbitCameraComponent::SetLockOnTarget で「自機の背後から対象を見る」カメラにし、
///   - リーダーのCharacterMotor::SetFacingTarget で対象の方を向いたまま移動(ストレイフ)させ、
///   - レティクル(Prefab)を対象の狙い点(IEnemy::GetLockOnPoint。敵ごとにInspectorで設定)へ置く。
/// 対象は「操作キャラ」に紐づくのではなくパーティ共通なので、キャラ切替しても注目は続く。
/// 術師のホーミング弾はGetTarget()で対象を取る。
/// </summary>
class LockOnController : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "LockOnController"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	/// <summary>注目中か。</summary>
	bool IsLockedOn() const { return target_ != nullptr; }
	/// <summary>注目対象(非注目ならnullptr)。</summary>
	KujataEngine::GameObject* GetTarget() const { return target_; }
	/// <summary>注目対象の狙い点(位置 + Lock On Height)。非注目ならゼロ。</summary>
	KujataEngine::Vector3 GetTargetPoint() const;

	/// <summary>注目のON/OFFを切り替える(ONにするとき候補が無ければ何もしない)。</summary>
	void Toggle();
	/// <summary>注目を外す。</summary>
	void Clear();
	/// <summary>隣の敵へ切り替える(direction: +1=画面右側の次、-1=左側の次)。候補が無ければそのまま。</summary>
	void SwitchTarget(int direction);

	/// <summary>シーン内のLockOnControllerを探す(魔法のホーミングなどが使う)。</summary>
	static LockOnController* FindInScene(KujataEngine::Scene* scene);

private:
	/// <summary>現在のリーダー(操作キャラ)。</summary>
	KujataEngine::GameObject* FindLeader() const;
	/// <summary>カメラ(OrbitCameraComponent持ち)のGameObject。</summary>
	KujataEngine::GameObject* FindCamera() const;
	/// <summary>targetが今も有効な注目対象か(シーンに存在・アクティブ・生存・距離内)。</summary>
	bool IsValidTarget(KujataEngine::GameObject* target, KujataEngine::GameObject* leader, float maxDistance) const;
	/// <summary>
	/// そのオブジェクトが注目してよい相手か。**狙えるのは敵(IEnemy)だけ**で、味方は倒れていても候補にしない。
	/// </summary>
	bool IsTargetableObject(KujataEngine::GameObject* object) const;
	/// <summary>候補(IEnemyを持ち狙える・Lock Distance以内)を列挙し、カメラ正面からの水平符号付き角度を添えて返す。</summary>
	struct Candidate {
		KujataEngine::GameObject* object = nullptr;
		float signedAngle = 0.0f; // カメラ正面基準の水平角[rad](右が正)
		float distance = 0.0f;
	};
	void CollectCandidates(std::vector<Candidate>& outCandidates) const;
	/// <summary>レティクルを出す/隠す/動かす。</summary>
	void UpdateReticle();
	/// <summary>リーダーが変わった/注目が変わったときに、体とカメラへ反映する。</summary>
	void ApplyToLeaderAndCamera();

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(cameraName_, "Camera Name", "OrbitCameraComponentを持つカメラのGameObject名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(reticlePrefabPath_, "Reticle Prefab",
		    "注目マーカーのPrefab(プロジェクトルート相対)。対象の狙い点へ毎フレーム置かれる。空なら出さない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(lockDistance_, "Lock Distance", 0.5f, 1.0f, 200.0f,
		    "注目を開始できる距離(リーダーからの水平距離)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(releaseDistance_, "Release Distance", 0.5f, 1.0f, 300.0f,
		    "注目が自動で外れる距離。Lock Distanceより大きくしてヒステリシスにする(境界でON/OFFがばたつかない)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(switchStickThreshold_, "Switch Stick Threshold", 0.05f, 0.1f, 1.0f,
		    "右スティックをこれ以上倒した瞬間に隣の敵へ切り替える。戻すまで再切替しない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(switchMinAngle_, "Switch Min Angle", 0.5f, 0.0f, 90.0f,
		    "切替先とみなす最小の角度差[deg]。同じ方向に重なっている敵へ誤って飛ばないためのしきい値。");
	}

	// カメラのGameObject名。
	KUJATA_FIELD_STRING(cameraName_, "Main Camera");
	// レティクルPrefab。
	KUJATA_FIELD_STRING(reticlePrefabPath_, "Prefabs/LockOnReticle.prefab.json");
	// 注目開始距離。
	KUJATA_FIELD_FLOAT(lockDistance_, 25.0f);
	// 自動解除距離。
	KUJATA_FIELD_FLOAT(releaseDistance_, 32.0f);
	// 切替スティック閾値。
	KUJATA_FIELD_FLOAT(switchStickThreshold_, 0.6f);
	// 切替の最小角度差[deg]。
	KUJATA_FIELD_FLOAT(switchMinAngle_, 3.0f);

	// --- ランタイム ---
	// 注目対象。
	KujataEngine::GameObject* target_ = nullptr;
	// 前フレームにストレイフ設定を入れたリーダー(切替時に解除するため)。
	KujataEngine::GameObject* appliedLeader_ = nullptr;
	// 右スティックが倒された状態か(エッジ検出)。
	bool stickHeld_ = false;
	// レティクルのオブジェクト(Prefabから初回だけ生成)。
	KujataEngine::GameObject* reticle_ = nullptr;
	// レティクルの生成を試みたか(失敗時に毎フレーム再試行しないため)。
	bool reticleTried_ = false;
};
