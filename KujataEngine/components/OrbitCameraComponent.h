#pragma once

#include "../math/Quaternion.h"
#include "../math/Vector3.h"
#include "../runtime/KujataApi.h"
#include "../scene/Component.h"
#include <string>

namespace KujataEngine {

/// <summary>
/// ターゲットを追従する三人称オービットカメラ(Cinemachineの3rd Person Follow相当)。
/// Main Camera のGameObjectへ追加して使う。Play中のみ動作する。
///
/// - 右スティックでyaw/pitchを操作(pitchはクランプ)
/// - 位置 = 注視点(ターゲット+高さオフセット)から姿勢方向へdistance離れた点
/// - 姿勢の減衰はQuaternionのSlerp、注視点の追従は指数平滑
/// - 所有GameObjectのTransformへ書き込むだけなので、既存CameraComponentがカメラへ同期する
/// </summary>
class KUJATA_API OrbitCameraComponent : public Component {
public:
	const char* GetTypeName() const override { return "OrbitCameraComponent"; }
	bool AllowMultiple() const override { return false; }

	void Update() override;
	void OnPlayStart() override;

	/// <summary>
	/// プレイヤー設定によるカメラ感度の倍率。**全カメラへ一律に掛かる**(既定1.0)。
	/// シーンに置いた Sensitivity X/Y は「作り手が決めた基準」として残し、
	/// 遊ぶ人の好みはこの倍率で乗せる、という切り分けにしてある。
	/// 値はエンジンDLL内のstaticなので、シーンを跨いでも保たれる。
	/// </summary>
	static void SetUserSensitivityScale(float scale);
	static float GetUserSensitivityScale();

	/// <summary>
	/// プレイヤー設定による上下反転。シーン側の Invert Y と**XOR**される
	/// (作り手が反転を仕込んだカメラでも、遊ぶ人の指定でさらに戻せる)。
	/// </summary>
	static void SetUserInvertY(bool invert);
	static bool GetUserInvertY();

	void DrawInspector() override;
	void WriteJson(nlohmann::json& json) const override;
	void ReadJson(const nlohmann::json& json) override;

	void SetTargetName(const std::string& targetName) { targetName_ = targetName; }
	const std::string& GetTargetName() const { return targetName_; }

	float GetYaw() const { return yaw_; }
	float GetPitch() const { return pitch_; }

	// --- Z注目(ロックオン) ---

	/// <summary>
	/// 注目対象を設定する。設定中は右スティックを無視し、「追従ターゲット(自機)の背後から対象を見る」向きへ
	/// yaw/pitchを自動で寄せる。対象の狙い点は対象位置 + targetHeight。
	/// 対象の生死・距離による解除はゲーム側の責任(対象が消える前に必ずClearLockOnTargetすること)。
	/// </summary>
	void SetLockOnTarget(GameObject* target, float targetHeight) {
		lockOnTarget_ = target;
		lockOnTargetHeight_ = targetHeight;
	}
	/// <summary>
	/// 追従距離に一時的な倍率を掛ける(致命の一撃などで寄る演出用)。1で通常。
	/// **戻す責任は設定した側にある。** 既存の遮蔽回避や減衰はそのまま働く。
	/// </summary>
	void UpdateCutscene();

	void SetDistanceScale(float scale) { distanceScale_ = (scale > 0.01f) ? scale : 0.01f; }

	/// <summary>
	/// **カットシーンとしてカメラを預かる。** 致命の一撃のように、数秒間まるごと絵を作りたいときに使う。
	/// 預けている間は追従も右スティックも無視し、毎フレーム渡された位置・注視点へ寄っていく。
	/// 寄せる速さ(blendSpeed)を有限にしてあるのは、通常視点から切り替わる瞬間に画がワープしないため。
	/// **戻す責任は預けた側にある。** EndCutsceneを呼ばないとカメラが固まったままになる。
	/// </summary>
	void BeginCutscene(float blendSpeed) {
		cutscene_ = true;
		cutsceneBlendSpeed_ = (blendSpeed > 0.0f) ? blendSpeed : 8.0f;
	}
	/// <summary>カットシーン中の目標を毎フレーム更新する。</summary>
	void SetCutsceneShot(const Vector3& position, const Vector3& lookAt) {
		cutscenePosition_ = position;
		cutsceneLookAt_ = lookAt;
	}
	void EndCutscene() { cutscene_ = false; }
	bool IsCutscene() const { return cutscene_; }
	float GetDistanceScale() const { return distanceScale_; }

	void ClearLockOnTarget() { lockOnTarget_ = nullptr; }
	bool IsLockedOn() const { return lockOnTarget_ != nullptr; }
	GameObject* GetLockOnTarget() const { return lockOnTarget_; }

private:
	// --- 設定(保存対象) ---
	std::string targetName_;                // 追従するGameObject名
	float distance_ = 6.0f;                 // 注視点からの距離
	float pivotHeight_ = 1.5f;              // ターゲット位置へ加える注視点の高さ
	float sensitivityX_ = 2.5f;             // 右スティックX感度[rad/s]
	float sensitivityY_ = 1.8f;             // 右スティックY感度[rad/s]
	bool invertY_ = false;                  // 上下反転
	float pitchMin_ = -1.2f;                // 見上げ限界[rad]
	float pitchMax_ = 1.2f;                 // 見下ろし限界[rad]
	float positionDamping_ = 10.0f;         // 注視点追従の速さ(大=機敏, 0=無効)
	float rotationDamping_ = 18.0f;         // スティック応答の速さ(大=機敏, 0=無効)

	// --- 遮蔽回避(Cinemachine Collider相当) ---
	bool collisionEnabled_ = true;          // 障害物でカメラを引き寄せるか
	float cameraRadius_ = 0.3f;             // カメラの半径(壁からのマージン)
	float minDistance_ = 0.5f;              // 引き寄せの下限距離
	float occlusionRecovery_ = 4.0f;        // 遮蔽解除後に距離が戻る速さ
	// 遮蔽物として扱うLayerのビットマスク。
	// 振り回される敵の部位(ガーディアンの脚など)を専用レイヤーへ移し、ここからそのビットを外すと、
	// 「相手の下に潜り込んだときカメラが部位に反応してガクガクする」のを断てる。
	uint32_t obstacleMask_ = 0xffffffff;

	// --- リセンタリング ---
	bool recenterEnabled_ = false;          // 無入力時にターゲット背後へ回り込むか
	float recenterWaitTime_ = 2.0f;         // 無入力からリセンタリング開始までの秒数
	float recenterSpeed_ = 3.0f;            // 回り込みの速さ

	// --- Z注目 ---
	float lockOnPitch_ = 0.25f;             // 注目中の基本ピッチ[rad](少し見下ろす)
	float lockOnDamping_ = 6.0f;            // 注目中の向き追従の速さ(大=機敏)
	float lockOnPivotBias_ = 0.15f;         // 注視点を自機→対象へどれだけ寄せるか(0=自機のまま, 0.5=中点)

	// --- 実行時状態 ---
	float distanceScale_ = 1.0f;            // 追従距離の一時倍率(演出用。1で通常)
	// --- カットシーン(致命など) ---
	bool cutscene_ = false;
	Vector3 cutscenePosition_ = {0.0f, 0.0f, 0.0f};
	Vector3 cutsceneLookAt_ = {0.0f, 0.0f, 0.0f};
	float cutsceneBlendSpeed_ = 8.0f;
	GameObject* lockOnTarget_ = nullptr;    // 注目対象(nullptr=非注目)
	float lockOnTargetHeight_ = 1.0f;       // 注目対象の狙い点の高さ
	float yaw_ = 0.0f;
	float pitch_ = 0.35f;
	Quaternion currentOrientation_ = Quaternion::Identity();
	Vector3 currentPivot_ = {0.0f, 0.0f, 0.0f};
	float currentDistance_ = 6.0f;
	float recenterTimer_ = 0.0f;
	bool snapNextUpdate_ = true;

	/// <summary>
	/// 注視点から理想位置への線分をシーンのColliderと判定し、遮蔽を考慮した距離を返します。
	/// </summary>
	float ComputeOccludedDistance(class Scene& scene, class GameObject* cameraOwner, class GameObject* target) const;
};

} // namespace KujataEngine
