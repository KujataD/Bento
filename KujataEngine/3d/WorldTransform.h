#pragma once

#include <d3d12.h>
#include <numbers>
#include <wrl.h>

#include <math/MathUtil.h>
#include <math/Quaternion.h>
#include "../runtime/KujataApi.h"

namespace KujataEngine {

/// <summary>
/// 定数バッファ用データ構造体（ワールド変換）
/// </summary>
struct TransformationMatrix {
	Matrix4x4 WVP;                   // ワールド・ビュー・プロジェクション合成行列
	Matrix4x4 World;                 // ワールド行列（法線変換などに使用）
	Matrix4x4 WorldInverseTranspose; // worldの逆転置行列
};

/// <summary>
/// ワールド変換データ
/// </summary>
class KUJATA_API WorldTransform {
public:
	// 定数バッファの本数。ビュー(Scene/Game/Shadow)ごとに別々のWVPを持つため。
	// ここでDirectXCommon.hをincludeするとWindows.hが広く波及するので定数を再宣言し、
	// DirectXCommon::kRenderViewCountとの一致は.cpp側のstatic_assertで守る。
	static constexpr uint32_t kViewBufferCount = 3;

	// スケール・回転・平行移動
	Vector3 scale_ = {1.0f, 1.0f, 1.0f};
	Vector3 rotation_ = {0.0f, 0.0f, 0.0f};
	Vector3 translation_ = {0.0f, 0.0f, 0.0f};

	// ワールド行列（UpdateMatrix後に有効）
	Matrix4x4 matWorld_ = MakeIdentity();

	// 親となるワールド変換へのポインタ（階層構造用）
	const WorldTransform* parent_ = nullptr;

	WorldTransform() = default;
	~WorldTransform() = default;

	/// <summary>
	/// 回転をQuaternionとして取得します(保存形式はEulerのまま)。
	/// 回転の合成・補間・減衰はQuaternionで計算し、書き戻しはSetRotationFromQuaternionを使います。
	/// </summary>
	Quaternion GetRotationQuaternion() const { return Quaternion::FromEuler(rotation_); }

	/// <summary>
	/// Quaternionの姿勢をEulerへ変換してrotation_に書き込みます。
	/// </summary>
	void SetRotationFromQuaternion(const Quaternion& rotation) { rotation_ = rotation.ToEuler(); }

	/// <summary>
	/// 初期化（定数バッファの生成・マッピング）
	/// </summary>
	void Initialize();

	void UpdateMatrix(const class Camera& camera, bool isBillboard = false);

	/// <summary>
	/// 親階層を考慮したビルボード行列でmatWorld_を更新します。
	/// ローカルtranslation_は親のワールド行列で変換され、平面はカメラを向きます。
	/// cameraLocalZ: カメラの視線方向へどれだけ手前(正)/奥(負)にずらすか(奥行き調整)。
	/// flipX: 表裏の反転(trueで裏面が手前)。
	/// </summary>
	void UpdateBillboardMatrix(const class Camera& camera, float cameraLocalZ, bool flipX);

	/// <summary>
	/// ワールド行列だけを更新する
	/// </summary>
	void UpdateWorldMatrix();

	void TransferMatrix(const Camera& camera) const;
	void TransferMatrix(const Camera& camera, const Matrix4x4& worldMatrix) const;

	/// <summary>
	/// Cameraを介さず、任意のビュープロジェクション行列でWVPを転送する。
	/// シャドウパスがライト視点の行列を渡すために使う(ライトはCameraを持たない)。
	/// 書き込み先は他と同じく現在のビュー番号の定数バッファなので、
	/// 呼ぶ前にDirectXCommon::SetRenderViewIndex(kShadowViewIndex)しておくこと。
	/// </summary>
	void TransferMatrixWithViewProjection(const Matrix4x4& viewProjection, const Matrix4x4& worldMatrix) const;

	TransformationMatrix GetMatrixData(const Camera& camera) const;
	TransformationMatrix GetBillboardMatrixData(const Camera& camera) const;

	void CalcRotationOfVelocity(const Vector3& velocity, const Vector3& deltaAngle = {0, 0, 0}, float maxRotationSpeed = 1.0f);


	// 現在描画中のビュー(DirectXCommonのrenderViewIndex)に対応する定数バッファを返す。
	// 同一フレームで複数ビューへ描いてもWVPが上書きされないよう、ビュー毎に別バッファを持つ。
	const Microsoft::WRL::ComPtr<ID3D12Resource>& GetConstBuffer() const;

	Vector3 GetWorldPosition() const { return {matWorld_.m[3][0], matWorld_.m[3][1], matWorld_.m[3][2]}; }
	void SetWorldPosition(Vector3 worldPos) {
		if (parent_) {
			Matrix4x4 inverseParent = Inverse(parent_->matWorld_);
			Vector3 localPos = Transform(worldPos, inverseParent);

			translation_ = localPos;
		} else {
			translation_ = worldPos;
		}
	}

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_[kViewBufferCount];
	// マッピング済みアドレス：各ビュー分
	mutable TransformationMatrix* constMap_[kViewBufferCount] = {};

	// コピー禁止
	WorldTransform(const WorldTransform&) = delete;
	WorldTransform& operator=(const WorldTransform&) = delete;
};

} // namespace KujataEngine
