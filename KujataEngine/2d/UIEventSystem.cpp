#include "UIEventSystem.h"

#include "../3d/Camera.h"
#include "../components/ButtonComponent.h"
#include "../components/CanvasComponent.h"
#include "../components/RectTransformComponent.h"
#include "../math/MathUtil.h"
#include "../runtime/UIInput.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include "UICanvasRenderer.h"
#include "UINavigationSystem.h"
#include "UIRect.h"
#include <cmath>
#include <memory>

namespace KujataEngine {
namespace {

// 押下開始したButton(離すまで保持)。Scene破棄で無効化されるためClearで消す。
ButtonComponent* gPressedButton = nullptr;

// 前フレームのポインタ位置。動いたらフォーカス操作からマウス操作へ切り替えるために持つ。
float gPrevPointerX = 0.0f;
float gPrevPointerY = 0.0f;
bool gHasPrevPointer = false;

// ノードを再帰処理: すべてのButtonをNormalへ戻しつつ、ポインタ下の最前面Button(描画順で最後)を探す。
void ProcessNode(GameObject* node, const UIRect& parentRect, float pointerCanvasX, float pointerCanvasY, ButtonComponent*& outHit) {
	if (!node || !node->IsActive()) {
		return;
	}
	RectTransformComponent* rectTransform = node->GetComponent<RectTransformComponent>();
	if (!rectTransform) {
		return;
	}
	const UIRect rect = rectTransform->ComputeRect(parentRect);

	ButtonComponent* button = node->GetComponent<ButtonComponent>();
	if (button && button->IsEnabled()) {
		button->ApplyVisualState(ButtonComponent::VisualState::Normal); // 静止状態へ戻す
		if (rect.Contains(pointerCanvasX, pointerCanvasY)) {
			outHit = button; // 後勝ち = 最前面
		}
	}

	for (GameObject* child : node->GetChildren()) {
		ProcessNode(child, rect, pointerCanvasX, pointerCanvasY, outHit);
	}
}

/// <summary>
/// ポインタが動いた/押されたらマウス操作モードへ戻す。
/// (パッドのフォーカスとマウスのホバーが同時にハイライトを奪い合わないようにするため)
/// </summary>
void UpdatePointerInputMode(const UIPointerState& pointer) {
	if (gHasPrevPointer) {
		const bool moved = std::fabs(pointer.x - gPrevPointerX) > 0.5f || std::fabs(pointer.y - gPrevPointerY) > 0.5f;
		if (moved || pointer.pressed) {
			SetUIInputMode(UIInputMode::Pointer);
		}
	}
	gPrevPointerX = pointer.x;
	gPrevPointerY = pointer.y;
	gHasPrevPointer = true;
}

/// <summary>
/// World Space Canvasに対し、ポインタから飛ばしたレイの当たり位置をキャンバス単位で求める。
/// Canvasのローカル空間へレイを移してz=0平面と交差させるので、Canvasが回転・拡縮していても正しく解ける。
/// </summary>
bool ComputeWorldCanvasPointer(const GameObject& canvasObject, const CanvasComponent::Layout& layout, const Camera& camera, float targetWidth, float targetHeight,
                               float pointerX, float pointerY, float& outCanvasX, float& outCanvasY) {
	// ポインタ(RTピクセル) → NDC → world空間のレイ。
	const float ndcX = (pointerX / targetWidth) * 2.0f - 1.0f;
	const float ndcY = 1.0f - (pointerY / targetHeight) * 2.0f;

	const Matrix4x4 inverseViewProjection = Inverse(camera.matView * camera.matProjection);
	const Vector3 nearWorld = Transform({ndcX, ndcY, 0.0f}, inverseViewProjection);
	const Vector3 farWorld = Transform({ndcX, ndcY, 1.0f}, inverseViewProjection);

	// レイをCanvasローカルへ移し、キャンバス平面(z=0)との交点を求める。
	const Matrix4x4 inverseCanvasWorld = Inverse(canvasObject.GetTransform().matWorld_);
	const Vector3 nearLocal = Transform(nearWorld, inverseCanvasWorld);
	const Vector3 farLocal = Transform(farWorld, inverseCanvasWorld);

	const float deltaZ = farLocal.z - nearLocal.z;
	if (std::fabs(deltaZ) < 1e-6f) {
		return false; // レイがキャンバス平面と平行。
	}
	const float t = -nearLocal.z / deltaZ;
	if (t < 0.0f || t > 1.0f) {
		return false; // 交点がニア〜ファーの外(カメラの後ろなど)。
	}

	const float localX = nearLocal.x + (farLocal.x - nearLocal.x) * t;
	const float localY = nearLocal.y + (farLocal.y - nearLocal.y) * t;

	// MakeCanvasUIToLocalMatrixの逆変換: local = (uiX - cw/2, -(uiY - ch/2))。
	outCanvasX = localX + layout.canvasWidth * 0.5f;
	outCanvasY = layout.canvasHeight * 0.5f - localY;
	return true;
}

} // namespace

void UpdateUIEventSystem(Scene& scene, float targetWidth, float targetHeight, Camera* camera) {
	if (targetWidth <= 0.0f || targetHeight <= 0.0f) {
		return;
	}
	const UIPointerState& pointer = GetUIPointer();
	UpdatePointerInputMode(pointer);

	ButtonComponent* hit = nullptr;
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		CanvasComponent* canvas = gameObject->GetComponent<CanvasComponent>();
		if (!canvas || !canvas->IsEnabled()) {
			continue;
		}
		const CanvasComponent::Layout layout = canvas->GetLayout(targetWidth, targetHeight);
		const UIRect rootRect{0.0f, 0.0f, layout.canvasWidth, layout.canvasHeight};

		// ポインタ位置をキャンバス単位へ変換する。ここだけがOverlayとWorld Spaceの違い。
		float pointerCanvasX = 0.0f;
		float pointerCanvasY = 0.0f;
		if (canvas->IsWorldSpace()) {
			if (!camera) {
				continue; // カメラ無しではworld空間の判定ができない。
			}
			if (!ComputeWorldCanvasPointer(*gameObject, layout, *camera, targetWidth, targetHeight, pointer.x, pointer.y, pointerCanvasX, pointerCanvasY)) {
				continue;
			}
		} else {
			pointerCanvasX = pointer.x / layout.scaleFactor;
			pointerCanvasY = pointer.y / layout.scaleFactor;
		}

		for (GameObject* child : gameObject->GetChildren()) {
			ProcessNode(child, rootRect, pointerCanvasX, pointerCanvasY, hit);
		}
	}

	// 上のループで全ButtonはNormalへ戻っている。ここから先は
	// ゲームパッド/キーボードのフォーカス操作かマウスのどちらか一方だけが見た目とクリックを担当する。
	if (UpdateUINavigation(scene, targetWidth, targetHeight)) {
		gPressedButton = nullptr; // マウスの押下途中でパッドへ持ち替えた場合、その押下は無かったことにする。
		return;
	}

	if (!pointer.inside) {
		hit = nullptr;
	}

	// 押下開始
	if (hit && hit->IsInteractable() && pointer.pressed) {
		gPressedButton = hit;
	}

	// ホバー/押下の見た目(ProcessNodeで全ButtonはNormal済みなので、対象だけ上書き)。
	if (hit && hit->IsInteractable()) {
		if (gPressedButton == hit && pointer.held) {
			hit->ApplyVisualState(ButtonComponent::VisualState::Pressed);
		} else {
			hit->ApplyVisualState(ButtonComponent::VisualState::Highlighted);
		}
	}

	// 離した瞬間: 押下開始と同じButton上ならクリック確定。
	if (pointer.released) {
		if (gPressedButton && gPressedButton == hit) {
			gPressedButton->FireOnClick();
		}
		gPressedButton = nullptr;
	}
}

} // namespace KujataEngine
