#include "WorldTransform.h"
#include "../base/DirectXCommon.h"
#include "Camera.h"
#include <cassert>
#include <numbers>

namespace KujataEngine {

// ヘッダ側でDirectXCommon.hをincludeできないため、本数の一致はここで担保する。
static_assert(WorldTransform::kViewBufferCount == DirectXCommon::kRenderViewCount, "WorldTransform::kViewBufferCount must match DirectXCommon::kRenderViewCount.");

namespace {
// 現在描画中のビュー番号(範囲外は0へ丸める)。
uint32_t CurrentViewIndex() {
	uint32_t index = DirectXCommon::GetInstance()->GetRenderViewIndex();
	return index < DirectXCommon::kRenderViewCount ? index : 0;
}
} // namespace

void WorldTransform::Initialize() {
	// ビュー毎に定数バッファを生成・マッピングし、単位行列で初期化する。
	matWorld_ = MakeIdentity();
	for (uint32_t viewIndex = 0; viewIndex < kViewBufferCount; ++viewIndex) {
		transformationMatrixResource_[viewIndex] = DirectXCommon::GetInstance()->CreateBufferResource(sizeof(TransformationMatrix));

		HRESULT hr = transformationMatrixResource_[viewIndex]->Map(0, nullptr, reinterpret_cast<void**>(&constMap_[viewIndex]));
		assert(SUCCEEDED(hr));

		constMap_[viewIndex]->WVP = MakeIdentity();
		constMap_[viewIndex]->World = MakeIdentity();
		constMap_[viewIndex]->WorldInverseTranspose = MakeIdentity();
	}
}

const Microsoft::WRL::ComPtr<ID3D12Resource>& WorldTransform::GetConstBuffer() const { return transformationMatrixResource_[CurrentViewIndex()]; }

void WorldTransform::UpdateMatrix(const Camera& camera, bool isBillboard) {
	if (isBillboard) {
		// ワールド行列の生成
		TransformationMatrix data = MakeBillboardMatrix(scale_, rotation_, translation_, camera);
		matWorld_ = data.World;
		*constMap_[CurrentViewIndex()] = data;
		return;
	}

	UpdateWorldMatrix();
	TransferMatrix(camera);
}

void WorldTransform::UpdateBillboardMatrix(const Camera& camera, float cameraLocalZ, bool flipX) {
	// 基準位置は親のワールド「位置」のみ(回転/スケールは使わない)。
	// 親の回転でローカルオフセットが公転してしまうのを防ぐ(HPバーが敵の自転で回るバグ対策)。
	Vector3 basePos = translation_;
	if (parent_) {
		basePos = parent_->GetWorldPosition();
	}

	Matrix4x4 invView = Inverse(camera.matView);

	// カメラ向きのビルボード回転(平行移動成分は除去)。
	Matrix4x4 billboardMatrix = kBackToFrontMatrix * invView;
	billboardMatrix.m[3][0] = 0.0f;
	billboardMatrix.m[3][1] = 0.0f;
	billboardMatrix.m[3][2] = 0.0f;

	Matrix4x4 rotateMatrix = MakeRotateZMatrix(rotation_.z) * billboardMatrix;

	// ローカルtranslation_(親からのオフセット)は、ビルボード姿勢で回して
	// 「カメラ相対の右/上」オフセットにする。これにより敵が自転してもバーは回らない。
	// ローカル半幅方向(x)へずらすと画面の左右、y は画面の上下に対応する。
	Vector3 localOffset = parent_ ? translation_ : Vector3{0.0f, 0.0f, 0.0f};
	Vector3 worldOffset = TransformNormal(localOffset, rotateMatrix);

	// カメラの視線方向へcameraLocalZ分だけ手前/奥へずらす(奥行き調整)。
	Vector3 camForward = Normalize({invView.m[2][0], invView.m[2][1], invView.m[2][2]});
	Vector3 worldPos = basePos + worldOffset - camForward * cameraLocalZ;

	// scale_は永続メンバなので、反転はローカルコピーに適用する(累積による破壊を防ぐ)。
	Vector3 drawScale = scale_;
	if (flipX) {
		drawScale.x *= -1.0f;
	}

	Matrix4x4 scaleMatrix = MakeScaleMatrix(drawScale);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(worldPos);

	matWorld_ = scaleMatrix * rotateMatrix * translateMatrix;
}

void WorldTransform::UpdateWorldMatrix() {
	// ローカルTransformからワールド行列を作り、親がある場合は親のワールド行列を合成する。
	matWorld_ = MakeAffineMatrix(scale_, rotation_, translation_);
	if (parent_) {
		matWorld_ = matWorld_ * parent_->matWorld_;
	}
}

void WorldTransform::TransferMatrix(const Camera& camera) const { TransferMatrix(camera, matWorld_); }

void WorldTransform::TransferMatrix(const Camera& camera, const Matrix4x4& worldMatrix) const {
	// WVP行列の生成
	Matrix4x4 matWVP = worldMatrix * camera.matView * camera.matProjection;

	// 現在ビューの定数バッファへ転送(他ビューのWVPを上書きしない)。
	TransformationMatrix* map = constMap_[CurrentViewIndex()];
	map->WVP = matWVP;
	map->World = worldMatrix;
	map->WorldInverseTranspose = Transpose(Inverse(worldMatrix));
}

void WorldTransform::TransferMatrixWithViewProjection(const Matrix4x4& viewProjection, const Matrix4x4& worldMatrix) const {
	TransformationMatrix* map = constMap_[CurrentViewIndex()];
	if (!map) {
		return;
	}
	map->WVP = worldMatrix * viewProjection;
	map->World = worldMatrix;
	map->WorldInverseTranspose = Transpose(Inverse(worldMatrix));
}

TransformationMatrix WorldTransform::GetMatrixData(const Camera& camera) const {
	TransformationMatrix data;

	Matrix4x4 matWVP = matWorld_ * camera.matView * camera.matProjection;

	data.WVP = matWVP;
	data.World = matWorld_;
	data.WorldInverseTranspose = Transpose(Inverse(matWorld_));

	return data;
}

TransformationMatrix WorldTransform::GetBillboardMatrixData(const Camera& camera) const {
	return MakeBillboardMatrix(scale_, rotation_, translation_, camera);
}

void WorldTransform::CalcRotationOfVelocity(const Vector3& velocity, const Vector3& deltaAngle, float maxRotationSpeed) {
	// Y軸周り角度(θy) ...atan2(高さ, 底辺)
	float targetY = std::atan2(velocity.x, velocity.z);
	// 横軸方向の長さを求める
	float velocityXZ = Length({velocity.x, 0.0f, velocity.z});
	// X軸周り角度(θx)
	float targetX = std::atan2(-velocity.y, velocityXZ);

	if (std::abs(rotation_.x - targetX) > maxRotationSpeed) {
		rotation_.x += maxRotationSpeed;
	}

	rotation_.x = targetX;
	rotation_.y = targetY;

	rotation_ += deltaAngle;
}

} // namespace KujataEngine
