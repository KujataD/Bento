#include "SceneGizmos.h"
#include "ISceneCamera.h"
#include "Scene.h"
#include "../3d/Camera.h"
#include "../3d/LineRenderer.h"
#include "../base/WinApp.h"
#include "../components/ColliderComponent.h"
#include "../components/PointLightComponent.h"
#include "../components/SpotLightComponent.h"
#include "../math/MathUtil.h"
#include "../runtime/SelectionProvider.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace KujataEngine {

namespace {

constexpr int32_t kColliderSphereSubdivision = 12;
constexpr float kColliderDebugPi = 3.14159265358979323846f;
constexpr Vector4 kColliderDebugColor = {0.1f, 1.0f, 0.35f, 1.0f};
constexpr Vector4 kCameraFrustumColor = {0.85f, 0.85f, 0.95f, 1.0f};
// ライトの影響範囲ギズモ(Unityと同じく黄色系)。内側コーンは減衰開始位置なので一段暗くする。
constexpr Vector4 kLightGizmoColor = {1.0f, 0.86f, 0.35f, 1.0f};
constexpr Vector4 kLightInnerConeColor = {0.75f, 0.62f, 0.22f, 1.0f};

// ゲーム中に全Colliderを可視化するデバッグモードのフラグ(Scene全体で共有)。
bool g_showAllColliders = false;

void DrawAABB(const AABB& aabb, const Vector4& color) {
	Vector3 corners[8] = {
	    {aabb.min.x, aabb.min.y, aabb.min.z},
        {aabb.max.x, aabb.min.y, aabb.min.z},
        {aabb.max.x, aabb.max.y, aabb.min.z},
        {aabb.min.x, aabb.max.y, aabb.min.z},
	    {aabb.min.x, aabb.min.y, aabb.max.z},
        {aabb.max.x, aabb.min.y, aabb.max.z},
        {aabb.max.x, aabb.max.y, aabb.max.z},
        {aabb.min.x, aabb.max.y, aabb.max.z},
	};

	LineRenderer::DrawLine(corners[0], corners[1], color);
	LineRenderer::DrawLine(corners[1], corners[2], color);
	LineRenderer::DrawLine(corners[2], corners[3], color);
	LineRenderer::DrawLine(corners[3], corners[0], color);

	LineRenderer::DrawLine(corners[4], corners[5], color);
	LineRenderer::DrawLine(corners[5], corners[6], color);
	LineRenderer::DrawLine(corners[6], corners[7], color);
	LineRenderer::DrawLine(corners[7], corners[4], color);

	LineRenderer::DrawLine(corners[0], corners[4], color);
	LineRenderer::DrawLine(corners[1], corners[5], color);
	LineRenderer::DrawLine(corners[2], corners[6], color);
	LineRenderer::DrawLine(corners[3], corners[7], color);
}

void DrawOBB(const OBB& obb, const Vector4& color) {
	Vector3 axisX = obb.orientations[0] * obb.size.x;
	Vector3 axisY = obb.orientations[1] * obb.size.y;
	Vector3 axisZ = obb.orientations[2] * obb.size.z;
	Vector3 corners[8] = {
	    obb.center - axisX - axisY - axisZ, obb.center + axisX - axisY - axisZ, obb.center + axisX + axisY - axisZ, obb.center - axisX + axisY - axisZ,
	    obb.center - axisX - axisY + axisZ, obb.center + axisX - axisY + axisZ, obb.center + axisX + axisY + axisZ, obb.center - axisX + axisY + axisZ,
	};

	LineRenderer::DrawLine(corners[0], corners[1], color);
	LineRenderer::DrawLine(corners[1], corners[2], color);
	LineRenderer::DrawLine(corners[2], corners[3], color);
	LineRenderer::DrawLine(corners[3], corners[0], color);

	LineRenderer::DrawLine(corners[4], corners[5], color);
	LineRenderer::DrawLine(corners[5], corners[6], color);
	LineRenderer::DrawLine(corners[6], corners[7], color);
	LineRenderer::DrawLine(corners[7], corners[4], color);

	LineRenderer::DrawLine(corners[0], corners[4], color);
	LineRenderer::DrawLine(corners[1], corners[5], color);
	LineRenderer::DrawLine(corners[2], corners[6], color);
	LineRenderer::DrawLine(corners[3], corners[7], color);
}

void DrawSphere(const Sphere& sphere, const Vector4& color) {
	if (sphere.radius <= 0.0f) {
		return;
	}

	float lonEvery = 2.0f * kColliderDebugPi / static_cast<float>(kColliderSphereSubdivision);
	float latEvery = kColliderDebugPi / static_cast<float>(kColliderSphereSubdivision);

	for (int32_t latIndex = 0; latIndex < kColliderSphereSubdivision; ++latIndex) {
		float lat = -kColliderDebugPi / 2.0f + latEvery * static_cast<float>(latIndex);

		for (int32_t lonIndex = 0; lonIndex < kColliderSphereSubdivision; ++lonIndex) {
			float lon = lonEvery * static_cast<float>(lonIndex);

			Vector3 a{};
			Vector3 b{};
			Vector3 c{};

			a.x = sphere.radius * std::cos(lat) * std::cos(lon) + sphere.center.x;
			a.y = sphere.radius * std::sin(lat) + sphere.center.y;
			a.z = sphere.radius * std::cos(lat) * std::sin(lon) + sphere.center.z;

			b.x = sphere.radius * std::cos(lat + latEvery) * std::cos(lon) + sphere.center.x;
			b.y = sphere.radius * std::sin(lat + latEvery) + sphere.center.y;
			b.z = sphere.radius * std::cos(lat + latEvery) * std::sin(lon) + sphere.center.z;

			c.x = sphere.radius * std::cos(lat) * std::cos(lon + lonEvery) + sphere.center.x;
			c.y = sphere.radius * std::sin(lat) + sphere.center.y;
			c.z = sphere.radius * std::cos(lat) * std::sin(lon + lonEvery) + sphere.center.z;

			LineRenderer::DrawLine(a, b, color);
			LineRenderer::DrawLine(a, c, color);
		}
	}
}

void DrawCapsule(const Capsule& capsule, const Vector4& color) {
	if (capsule.radius <= 0.0f) {
		return;
	}

	const float radius = capsule.radius;
	Vector3 axisVector = capsule.p1 - capsule.p0;
	float axisLength = Length(axisVector);
	Vector3 axis = (axisLength > 1e-6f) ? axisVector * (1.0f / axisLength) : Vector3{0.0f, 1.0f, 0.0f};

	// axisに垂直な正規直交基底(right, forward)を作る。
	Vector3 reference = (std::abs(axis.y) < 0.99f) ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
	Vector3 right = Normalize(Cross(reference, axis));
	Vector3 forward = Cross(axis, right);

	const int32_t segment = kColliderSphereSubdivision;
	const float twoPi = 2.0f * kColliderDebugPi;

	// 両端の円(axisに垂直な面)。
	auto drawRing = [&](const Vector3& center) {
		for (int32_t i = 0; i < segment; ++i) {
			float t0 = twoPi * (static_cast<float>(i) / static_cast<float>(segment));
			float t1 = twoPi * (static_cast<float>(i + 1) / static_cast<float>(segment));
			Vector3 a = center + (right * std::cos(t0) + forward * std::sin(t0)) * radius;
			Vector3 b = center + (right * std::cos(t1) + forward * std::sin(t1)) * radius;
			LineRenderer::DrawLine(a, b, color);
		}
	};
	drawRing(capsule.p0);
	drawRing(capsule.p1);

	// 半球のシルエット弧(0..pi の半円)。axisSignで膨らむ向きを切り替える。
	auto drawArc = [&](const Vector3& center, const Vector3& sideDir, float axisSign) {
		for (int32_t i = 0; i < segment; ++i) {
			float f0 = kColliderDebugPi * (static_cast<float>(i) / static_cast<float>(segment));
			float f1 = kColliderDebugPi * (static_cast<float>(i + 1) / static_cast<float>(segment));
			Vector3 a = center + (sideDir * std::cos(f0) + axis * (axisSign * std::sin(f0))) * radius;
			Vector3 b = center + (sideDir * std::cos(f1) + axis * (axisSign * std::sin(f1))) * radius;
			LineRenderer::DrawLine(a, b, color);
		}
	};
	drawArc(capsule.p1, right, 1.0f);
	drawArc(capsule.p1, forward, 1.0f);
	drawArc(capsule.p0, right, -1.0f);
	drawArc(capsule.p0, forward, -1.0f);

	// cylinder側面(4本)。
	LineRenderer::DrawLine(capsule.p0 + right * radius, capsule.p1 + right * radius, color);
	LineRenderer::DrawLine(capsule.p0 - right * radius, capsule.p1 - right * radius, color);
	LineRenderer::DrawLine(capsule.p0 + forward * radius, capsule.p1 + forward * radius, color);
	LineRenderer::DrawLine(capsule.p0 - forward * radius, capsule.p1 - forward * radius, color);
}

// Trigger Colliderは通常Colliderと色を変えて描く(黄=Trigger, 緑=通常)。
constexpr Vector4 kTriggerColliderDebugColor = {1.0f, 0.85f, 0.1f, 1.0f};

// 1つのGameObjectが持つColliderをすべてワイヤーフレームで描く。
void DrawGameObjectColliders(GameObject& gameObject) {
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (!component || !component->IsEnabled()) {
			continue;
		}

		ColliderComponent* collider = dynamic_cast<ColliderComponent*>(component.get());
		if (!collider) {
			continue;
		}

		const Vector4& color = collider->IsTrigger() ? kTriggerColliderDebugColor : kColliderDebugColor;
		if (collider->GetShapeType() == ColliderShapeType::Sphere) {
			DrawSphere(collider->GetWorldSphere(), color);
		} else if (collider->GetShapeType() == ColliderShapeType::Box) {
			BoxColliderComponent* boxCollider = dynamic_cast<BoxColliderComponent*>(collider);
			if (boxCollider && boxCollider->UsesWorldOBB()) {
				DrawOBB(boxCollider->GetWorldOBB(), color);
			} else {
				DrawAABB(collider->GetWorldAABB(), color);
			}
		} else if (collider->GetShapeType() == ColliderShapeType::Capsule) {
			CapsuleColliderComponent* capsuleCollider = dynamic_cast<CapsuleColliderComponent*>(collider);
			if (capsuleCollider) {
				DrawCapsule(capsuleCollider->GetWorldCapsule(), color);
			}
		}
	}
}

// 自分と、その配下すべてのColliderを描く。
void DrawGameObjectCollidersRecursive(GameObject& gameObject) {
	if (!gameObject.IsActiveInHierarchy()) {
		return;
	}
	DrawGameObjectColliders(gameObject);
	for (GameObject* child : gameObject.GetChildren()) {
		if (child) {
			DrawGameObjectCollidersRecursive(*child);
		}
	}
}

// カメラの視錘台(フラスタム)をワイヤーフレームで描く。Unityのカメラ選択時のギズモ相当。
// 実far(既定1000)は大きすぎるので、向きが分かる固定サイズ(おおよそ5x5x5に収まる)で描く。
// 断面の縦横比は画面(ウィンドウ)の縦横比に沿わせる。cornersはカメラローカル(+Z前方)で作り
// worldMatrixでワールドへ変換する。
void DrawCameraFrustum(const Camera& camera, const Matrix4x4& worldMatrix, const Vector4& color) {
	const float tanHalfFovY = std::tan(camera.fovAngleY * 0.5f);
	const float screenAspect = static_cast<float>(WinApp::kWindowWidth) / static_cast<float>(WinApp::kWindowHeight);

	// far側の奥行きを固定し、半幅/半高/奥行きのどれかが2.5(=5の半分)を超えたら等倍で縮める。
	const float kHalfBox = 2.5f;
	float farDistance = 2.5f;
	const float farHalfHeight = tanHalfFovY * farDistance;
	const float farHalfWidth = farHalfHeight * screenAspect;
	const float maxExtent = (std::max)(farDistance, (std::max)(farHalfWidth, farHalfHeight));
	if (maxExtent > kHalfBox) {
		farDistance *= kHalfBox / maxExtent;
	}
	const float nearDistance = farDistance * 0.1f; // near平面は小さく(四角錐に近い見た目)

	auto MakePlaneCorners = [&](float distance, Vector3 outCorners[4]) {
		const float halfHeight = tanHalfFovY * distance;
		const float halfWidth = halfHeight * screenAspect;
		const Vector3 localCorners[4] = {
		    {-halfWidth, -halfHeight, distance},
		    {halfWidth, -halfHeight, distance},
		    {halfWidth, halfHeight, distance},
		    {-halfWidth, halfHeight, distance},
		};
		for (int i = 0; i < 4; ++i) {
			outCorners[i] = Transform(localCorners[i], worldMatrix);
		}
	};

	Vector3 nearCorners[4];
	Vector3 farCorners[4];
	MakePlaneCorners(nearDistance, nearCorners);
	MakePlaneCorners(farDistance, farCorners);

	for (int i = 0; i < 4; ++i) {
		const int next = (i + 1) % 4;
		LineRenderer::DrawLine(nearCorners[i], nearCorners[next], color); // near平面
		LineRenderer::DrawLine(farCorners[i], farCorners[next], color);   // far平面
		LineRenderer::DrawLine(nearCorners[i], farCorners[i], color);     // 側面エッジ
	}
}

// SpotLightの照射コーンをワイヤーで描く。Unityのスポットライト選択時のギズモ相当。
// apexから前方rangeの位置に外側角の円を置き、apexとの間を4本のエッジで結ぶ。
// 内側角(減衰開始)の円も同じ距離に薄く描いて、Inner Spot Angleの効きが見えるようにする。
void DrawSpotLightCone(const Vector3& apex, const Vector3& direction, float range, float spotAngleDegrees, float innerSpotAngleDegrees, const Vector4& color) {
	if (range <= 0.0f) {
		return;
	}

	// 照射軸に垂直な基底を作る。directionがほぼY軸のときだけ別の軸を種にする。
	Vector3 seed = (std::abs(direction.y) > 0.99f) ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{0.0f, 1.0f, 0.0f};
	Vector3 right = Normalize(Cross(seed, direction));
	Vector3 up = Normalize(Cross(direction, right));

	const Vector3 center = apex + direction * range;

	auto DrawAngleCircle = [&](float angleDegrees, const Vector4& circleColor, bool drawEdges) {
		// 全開き角なので半分がコーンの半頂角。円の半径は range * tan(半頂角)。
		const float halfAngle = angleDegrees * 0.5f * (kColliderDebugPi / 180.0f);
		const float radius = range * std::tan((std::min)(halfAngle, kColliderDebugPi * 0.5f - 0.01f));

		const int32_t segment = kColliderSphereSubdivision * 2;
		Vector3 previous{};
		for (int32_t i = 0; i <= segment; ++i) {
			const float theta = 2.0f * kColliderDebugPi * (static_cast<float>(i) / static_cast<float>(segment));
			const Vector3 point = center + right * (std::cos(theta) * radius) + up * (std::sin(theta) * radius);
			if (i > 0) {
				LineRenderer::DrawLine(previous, point, circleColor);
			}
			// エッジは4方向だけ引く(全周に引くと塗り潰しになって向きが読めない)。
			if (drawEdges && (i % (segment / 4) == 0) && i < segment) {
				LineRenderer::DrawLine(apex, point, circleColor);
			}
			previous = point;
		}
	};

	// 照射軸。コーンが細いときでも向きが分かるように必ず引く。
	LineRenderer::DrawLine(apex, center, color);
	DrawAngleCircle(spotAngleDegrees, color, true);
	if (innerSpotAngleDegrees > 0.0f && innerSpotAngleDegrees < spotAngleDegrees) {
		DrawAngleCircle(innerSpotAngleDegrees, kLightInnerConeColor, false);
	}
}

} // namespace

void SceneGizmos::DrawSelectedColliders(const Scene& scene) {
	// 出し分けは呼び出し側(RenderViewのdrawEditorOverlays)が担当する。Sceneビューはプレイ中も表示。
	GameObject* selectedObject = GetSelectionProvider().GetSelectedGameObject();
	if (!selectedObject || !selectedObject->IsActiveInHierarchy()) {
		return;
	}
	if (scene.FindGameObjectByInstanceId(selectedObject->GetInstanceId()) != selectedObject) {
		return;
	}

	// **選んだものだけでなく、その配下も描く。**
	// Colliderを子オブジェクトに分けて持たせている構成(ステージの床/壁など)だと、
	// 親を選んでも一本も出ず、子を1つずつ選んで回らないと形が見えない。
	// 動かすのは親なので、親を選んだ時点で配下がまとめて見えないと調整ができない。
	DrawGameObjectCollidersRecursive(*selectedObject);
}

// シーン内の全アクティブGameObjectのColliderを描く(ゲーム中の当たり判定確認用)。
void SceneGizmos::DrawAllColliders(const Scene& scene) {
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		DrawGameObjectColliders(*gameObject);
	}
}

// 選択中のGameObjectがCameraComponentを持つ場合、そのフラスタムを描く。
void SceneGizmos::DrawSelectedCameraFrustum(const Scene& scene) {
	// 出し分けは呼び出し側(RenderViewのdrawEditorOverlays)が担当する。
	GameObject* selectedObject = GetSelectionProvider().GetSelectedGameObject();
	if (!selectedObject || !selectedObject->IsActiveInHierarchy()) {
		return;
	}
	if (scene.FindGameObjectByInstanceId(selectedObject->GetInstanceId()) != selectedObject) {
		return;
	}

	for (const std::unique_ptr<Component>& component : selectedObject->GetComponents()) {
		if (!component || !component->IsEnabled()) {
			continue;
		}
		const ISceneCamera* sceneCamera = dynamic_cast<const ISceneCamera*>(component.get());
		if (!sceneCamera) {
			continue;
		}
		const Camera* camera = sceneCamera->GetSceneCamera();
		if (!camera) {
			continue;
		}
		// カメラが実際に描画へ使う姿勢(=ローカル回転+ワールド位置。SyncFromOwnerTransform参照)で錐体を描く。
		// オーナーのmatWorld_(親の回転/スケールを含む)を使うとGameビューの見え方とズレるため、
		// カメラ自身の向きに合わせる(親が回転していても錐体は実際の描画と一致する)。
		Matrix4x4 cameraWorld = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, camera->rotation_, camera->translation_);
		DrawCameraFrustum(*camera, cameraWorld, kCameraFrustumColor);
	}
}

// 選択中のGameObjectが持つライトの影響範囲を描く。
// PointLightはradiusの球、SpotLightは照射コーン(Unityのライトギズモ相当)。
void SceneGizmos::DrawSelectedLightRanges(const Scene& scene) {
	// 出し分けは呼び出し側(RenderViewのdrawEditorOverlays)が担当する。
	GameObject* selectedObject = GetSelectionProvider().GetSelectedGameObject();
	if (!selectedObject || !selectedObject->IsActiveInHierarchy()) {
		return;
	}
	if (scene.FindGameObjectByInstanceId(selectedObject->GetInstanceId()) != selectedObject) {
		return;
	}

	for (const std::unique_ptr<Component>& component : selectedObject->GetComponents()) {
		if (!component || !component->IsEnabled()) {
			continue;
		}

		PointLightComponent* pointLight = dynamic_cast<PointLightComponent*>(component.get());
		if (pointLight) {
			const PointLightData& data = pointLight->GetData();
			if (data.radius > 0.0f) {
				DrawSphere({selectedObject->GetTransform().GetWorldPosition(), data.radius}, kLightGizmoColor);
			}
			continue;
		}

		SpotLightComponent* spotLight = dynamic_cast<SpotLightComponent*>(component.get());
		if (spotLight) {
			DrawSpotLightCone(
			    selectedObject->GetTransform().GetWorldPosition(),
			    spotLight->GetWorldDirection(),
			    spotLight->GetRange(),
			    spotLight->GetSpotAngleDegrees(),
			    spotLight->GetInnerSpotAngleDegrees(),
			    kLightGizmoColor);
		}
	}
}

void SceneGizmos::DrawGrid(const Vector4& color) {
	const float kGridHalfWidth = 100.0f;
	const uint32_t kSubdivision = 100;
	const float kGridEvery = (kGridHalfWidth * 2.0f) / float(kSubdivision);

	for (uint32_t xIndex = 0; xIndex <= kSubdivision; ++xIndex) {
		Vector3 lineStart = {-kGridHalfWidth + xIndex * kGridEvery, 0.0f, -kGridHalfWidth};
		Vector3 lineEnd = {-kGridHalfWidth + xIndex * kGridEvery, 0.0f, kGridHalfWidth};

		LineRenderer::DrawLine(lineStart, lineEnd, color);
	}

	for (uint32_t zIndex = 0; zIndex <= kSubdivision; ++zIndex) {
		Vector3 lineStart = {-kGridHalfWidth, 0.0f, -kGridHalfWidth + zIndex * kGridEvery};
		Vector3 lineEnd = {kGridHalfWidth, 0.0f, -kGridHalfWidth + zIndex * kGridEvery};

		LineRenderer::DrawLine(lineStart, lineEnd, color);
	}
}

bool SceneGizmos::IsShowAllColliders() { return g_showAllColliders; }

void SceneGizmos::SetShowAllColliders(bool show) { g_showAllColliders = show; }

} // namespace KujataEngine
