#include "UICanvasRenderer.h"

#include "../3d/Camera.h"
#include "../components/CanvasComponent.h"
#include "../components/ImageComponent.h"
#include "../components/RectTransformComponent.h"
#include "../components/TextComponent.h"
#include "../math/MathUtil.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include "UIRect.h"
#include "UIRenderer.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace KujataEngine {
namespace {

// 1つのUIノード(RectTransformを持つGameObject)を描画し、子へ再帰する。
void DrawUINode(GameObject* node, const UIRect& parentRect, float scaleFactor) {
	if (!node || !node->IsActive()) {
		return;
	}
	RectTransformComponent* rectTransform = node->GetComponent<RectTransformComponent>();
	if (!rectTransform) {
		// RectTransformを持たない=UI要素でない。この枝は描画しない。
		return;
	}

	const UIRect rect = rectTransform->ComputeRect(parentRect);

	ImageComponent* image = node->GetComponent<ImageComponent>();
	if (image && image->IsEnabled()) {
		image->DrawUI(rect, scaleFactor);
	}

	TextComponent* text = node->GetComponent<TextComponent>();
	if (text && text->IsEnabled()) {
		text->DrawUI(rect, scaleFactor);
	}

	for (GameObject* child : node->GetChildren()) {
		DrawUINode(child, rect, scaleFactor);
	}
}

// 描画順(DrawUINodeと同じDFS)でUI要素のピクセル矩形を集める。
void CollectUINode(GameObject* node, GameObject* canvasObject, const UIRect& parentRect, float scaleFactor, std::vector<UIElementRectInfo>& out) {
	if (!node || !node->IsActive()) {
		return;
	}
	RectTransformComponent* rectTransform = node->GetComponent<RectTransformComponent>();
	if (!rectTransform) {
		return;
	}
	const UIRect rect = rectTransform->ComputeRect(parentRect);

	UIElementRectInfo info;
	info.object = node;
	info.canvasObject = canvasObject;
	info.pixelRect = {rect.x * scaleFactor, rect.y * scaleFactor, rect.width * scaleFactor, rect.height * scaleFactor};
	info.scaleFactor = scaleFactor;
	out.push_back(info);

	for (GameObject* child : node->GetChildren()) {
		CollectUINode(child, canvasObject, rect, scaleFactor, out);
	}
}

// 準備(テクスチャ/フォント読み込み)を再帰で行う。描画はしない。
void PrepareUINode(GameObject* node) {
	if (!node || !node->IsActive()) {
		return;
	}
	if (!node->GetComponent<RectTransformComponent>()) {
		return;
	}
	if (ImageComponent* image = node->GetComponent<ImageComponent>(); image && image->IsEnabled()) {
		image->Prepare();
	}
	if (TextComponent* text = node->GetComponent<TextComponent>(); text && text->IsEnabled()) {
		text->Prepare();
	}
	for (GameObject* child : node->GetChildren()) {
		PrepareUINode(child);
	}
}

} // namespace

void PrepareSceneCanvases(Scene& scene) {
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		CanvasComponent* canvas = gameObject->GetComponent<CanvasComponent>();
		if (canvas && canvas->IsEnabled()) {
			for (GameObject* child : gameObject->GetChildren()) {
				PrepareUINode(child);
			}
		}
	}
}

Matrix4x4 MakeCanvasUIToLocalMatrix(float canvasWidth, float canvasHeight) {
	// UI座標(左上原点・Y下) → Canvasローカル(中心原点・Y上)。
	// 行ベクトル規約なので Scale → Translate の順に掛ける。
	//   local = (uiX - cw/2, -(uiY - ch/2))
	return MakeScaleMatrix({1.0f, -1.0f, 1.0f}) * MakeTranslateMatrix({-canvasWidth * 0.5f, canvasHeight * 0.5f, 0.0f});
}

void DrawSceneCanvases(Scene& scene, float targetWidth, float targetHeight) {
	if (targetWidth <= 0.0f || targetHeight <= 0.0f) {
		return;
	}

	// Screen Space - OverlayのCanvasを収集し、sortOrder順に並べる(World Spaceは別パス)。
	std::vector<std::pair<int, GameObject*>> canvases;
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		CanvasComponent* canvas = gameObject->GetComponent<CanvasComponent>();
		if (canvas && canvas->IsEnabled() && !canvas->IsWorldSpace()) {
			canvases.emplace_back(canvas->GetSortOrder(), gameObject.get());
		}
	}
	if (canvases.empty()) {
		return;
	}
	std::stable_sort(canvases.begin(), canvases.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

	UIRenderer::Begin(targetWidth, targetHeight);
	for (const auto& [sortOrder, canvasObject] : canvases) {
		CanvasComponent* canvas = canvasObject->GetComponent<CanvasComponent>();
		const CanvasComponent::Layout layout = canvas->GetLayout(targetWidth, targetHeight);
		const UIRect rootRect{0.0f, 0.0f, layout.canvasWidth, layout.canvasHeight};

		// Canvas直下の子から順に描画(Canvas自身は器なので描かない)。
		for (GameObject* child : canvasObject->GetChildren()) {
			DrawUINode(child, rootRect, layout.scaleFactor);
		}
	}
	UIRenderer::End();
}

void DrawSceneWorldCanvases(Scene& scene, Camera* camera, float targetWidth, float targetHeight) {
	if (!camera || targetWidth <= 0.0f || targetHeight <= 0.0f) {
		return;
	}

	std::vector<std::pair<int, GameObject*>> canvases;
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		CanvasComponent* canvas = gameObject->GetComponent<CanvasComponent>();
		if (canvas && canvas->IsEnabled() && canvas->IsWorldSpace()) {
			canvases.emplace_back(canvas->GetSortOrder(), gameObject.get());
		}
	}
	if (canvases.empty()) {
		return;
	}
	std::stable_sort(canvases.begin(), canvases.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

	const Matrix4x4 viewProjection = camera->matView * camera->matProjection;

	for (const auto& [sortOrder, canvasObject] : canvases) {
		CanvasComponent* canvas = canvasObject->GetComponent<CanvasComponent>();
		const CanvasComponent::Layout layout = canvas->GetLayout(targetWidth, targetHeight);
		const UIRect rootRect{0.0f, 0.0f, layout.canvasWidth, layout.canvasHeight};

		// キャンバス単位の頂点 → Canvasローカル → world(Transform) → クリップ空間。
		const Matrix4x4 uiToLocal = MakeCanvasUIToLocalMatrix(layout.canvasWidth, layout.canvasHeight);
		const Matrix4x4 canvasToClip = uiToLocal * canvasObject->GetTransform().matWorld_ * viewProjection;

		// Canvasごとに行列が変わるのでBeginWorldもCanvasごとに呼ぶ。
		UIRenderer::BeginWorld(canvasToClip, targetWidth, targetHeight);
		for (GameObject* child : canvasObject->GetChildren()) {
			// scaleFactorは1(キャンバス単位のまま頂点にする)。ピクセル換算はしない。
			DrawUINode(child, rootRect, 1.0f);
		}
		UIRenderer::End();
	}
}

bool IsUIRelatedObject(GameObject* object) {
	// 自分自身がCanvas、または祖先のいずれかがCanvasならUI関連。
	for (GameObject* current = object; current != nullptr; current = current->GetParent()) {
		if (current->GetComponent<CanvasComponent>()) {
			return true;
		}
	}
	return false;
}

void CollectSceneUIElementRects(Scene& scene, float targetWidth, float targetHeight, std::vector<UIElementRectInfo>& out) {
	out.clear();
	if (targetWidth <= 0.0f || targetHeight <= 0.0f) {
		return;
	}

	// 描画と同じくsortOrder順に走査する(後のCanvasほど手前)。
	std::vector<std::pair<int, GameObject*>> canvases;
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		CanvasComponent* canvas = gameObject->GetComponent<CanvasComponent>();
		// 矩形はRTピクセル空間として返す契約なので、World Space Canvasは対象外(Transformギズモで操作する)。
		if (canvas && canvas->IsEnabled() && !canvas->IsWorldSpace()) {
			canvases.emplace_back(canvas->GetSortOrder(), gameObject.get());
		}
	}
	if (canvases.empty()) {
		return;
	}
	std::stable_sort(canvases.begin(), canvases.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

	for (const auto& [sortOrder, canvasObject] : canvases) {
		CanvasComponent* canvas = canvasObject->GetComponent<CanvasComponent>();
		const CanvasComponent::Layout layout = canvas->GetLayout(targetWidth, targetHeight);
		const UIRect rootRect{0.0f, 0.0f, layout.canvasWidth, layout.canvasHeight};
		for (GameObject* child : canvasObject->GetChildren()) {
			CollectUINode(child, canvasObject, rootRect, layout.scaleFactor, out);
		}
	}
}

bool ComputeUIElementRect(GameObject* node, float targetWidth, float targetHeight, UIRect& outRect, float& outScaleFactor) {
	if (!node || targetWidth <= 0.0f || targetHeight <= 0.0f) {
		return false;
	}

	// nodeからCanvas祖先まで遡り、Canvas直下→nodeの順の連鎖を作る。
	std::vector<GameObject*> chain;
	GameObject* canvasObject = nullptr;
	for (GameObject* current = node; current != nullptr; current = current->GetParent()) {
		if (current->GetComponent<CanvasComponent>()) {
			canvasObject = current;
			break;
		}
		chain.push_back(current);
	}
	if (!canvasObject) {
		return false;
	}

	CanvasComponent* canvas = canvasObject->GetComponent<CanvasComponent>();
	// この関数はRTピクセル空間の矩形を返す契約。World Spaceは座標系が違うので扱わない。
	if (canvas->IsWorldSpace()) {
		return false;
	}
	const CanvasComponent::Layout layout = canvas->GetLayout(targetWidth, targetHeight);
	UIRect rect{0.0f, 0.0f, layout.canvasWidth, layout.canvasHeight};

	// chainはnode..Canvas直下の順なので、逆順(上位→下位)で矩形を解決していく。
	for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
		RectTransformComponent* rectTransform = (*it)->GetComponent<RectTransformComponent>();
		if (!rectTransform) {
			return false;
		}
		rect = rectTransform->ComputeRect(rect);
	}

	outRect = rect;
	outScaleFactor = layout.scaleFactor;
	return true;
}

} // namespace KujataEngine
