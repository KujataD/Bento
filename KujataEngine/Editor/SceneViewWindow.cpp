#include "SceneViewWindow.h"

#include "../../externals/ImGuizmo-1.9/src/ImGuizmo.h"
#include "../2d/UICanvasRenderer.h"
#include "../3d/Camera.h"
#include "../3d/WorldTransform.h"
#include "../base/DirectXCommon.h"
#include "../base/ProjectPath.h"
#include "../base/TextureManager.h"
#include "../components/CanvasComponent.h"
#include "../components/RectTransformComponent.h"
#include "../postprocess/PostProcess.h"
#include "../runtime/PlayState.h"
#include "../math/MathUtil.h"
#include "../math/Vector2.h"
#include "../scene/GameObject.h"
#include "../scene/RayCast.h"
#include "../scene/Scene.h"
#include "EditorApplication.h"
#include "EditorImGuiUtil.h"
#include "EditorSelection.h"
#include <cmath>
#include <d3d12.h>
#include <vector>

namespace KujataEngine {
namespace {

constexpr float kDegreesToRadians = 0.017453292519943295f;

float* MatrixData(Matrix4x4& matrix) { return &matrix.m[0][0]; }

const float* MatrixData(const Matrix4x4& matrix) { return &matrix.m[0][0]; }

// 目標矩形(キャンバス単位)から anchoredPosition / sizeDelta を逆算して設定する。
// アンカー/ピボットは変えずに、見た目の矩形をtargetに一致させる。移動・リサイズ共通。
void ApplyRectToRectTransform(RectTransformComponent* rt, const UIRect& parentRect, const UIRect& target) {
	const Vector2 aMin = rt->GetAnchorMin();
	const Vector2 aMax = rt->GetAnchorMax();
	const Vector2 pivot = rt->GetPivot();

	const float aMinX = parentRect.x + aMin.x * parentRect.width;
	const float aMaxX = parentRect.x + aMax.x * parentRect.width;
	const float aMinY = parentRect.y + aMin.y * parentRect.height;
	const float aMaxY = parentRect.y + aMax.y * parentRect.height;

	// sizeDelta = 目標サイズ - アンカー間距離。
	const float sizeDx = target.width - (aMaxX - aMinX);
	const float sizeDy = target.height - (aMaxY - aMinY);

	// anchoredPosition = ピボット位置 - アンカー参照点。
	const float anchorRefX = aMinX + (aMaxX - aMinX) * pivot.x;
	const float anchorRefY = aMinY + (aMaxY - aMinY) * pivot.y;
	const float pivotPosX = target.x + pivot.x * target.width;
	const float pivotPosY = target.y + pivot.y * target.height;

	rt->SetSizeDelta({sizeDx, sizeDy});
	rt->SetAnchoredPosition({pivotPosX - anchorRefX, pivotPosY - anchorRefY});
}

// value を guides の中で最も近い線にスナップする(閾値内のみ)。snapped/outGuideに結果を返す。
float SnapToGuides(float value, const std::vector<float>& guides, float threshold, bool& snapped, float& outGuide) {
	snapped = false;
	outGuide = value;
	float best = threshold;
	float result = value;
	for (float g : guides) {
		const float d = std::fabs(value - g);
		if (d <= best) {
			best = d;
			result = g;
			outGuide = g;
			snapped = true;
		}
	}
	return result;
}

} // namespace

void SceneViewWindow::LoadGizmoIcons() {
#ifdef USE_IMGUI
	if (gizmoIconsLoaded_) {
		return;
	}

	std::filesystem::path imageDirectory = GetEditorIconDirectory();
	TextureManager* textureManager = TextureManager::GetInstance();

	textureManager->TryLoadTexture((imageDirectory / "icon_guizmo_translate.png").string(), gizmoTranslateIconIndex_);
	textureManager->TryLoadTexture((imageDirectory / "icon_guizmo_rotate.png").string(), gizmoRotateIconIndex_);
	textureManager->TryLoadTexture((imageDirectory / "icon_guizmo_scale.png").string(), gizmoScaleIconIndex_);

	gizmoIconsLoaded_ = true;
#endif // USE_IMGUI
}

bool SceneViewWindow::DrawGizmoModeButton(const char* id, const char* fallbackLabel, uint32_t textureIndex, TransformGizmoOperation operation, const char* tooltip) {
#ifdef USE_IMGUI
	bool selected = gizmoOperation_ == operation;
	if (selected) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	}

	bool clicked = false;
	if (textureIndex != 0) {
		D3D12_GPU_DESCRIPTOR_HANDLE handle = TextureManager::GetInstance()->GetSrvHandle(textureIndex);
		clicked = ImGui::ImageButton(id, static_cast<ImTextureID>(handle.ptr), ImVec2(22.0f, 22.0f));
	} else {
		clicked = ImGui::Button(fallbackLabel, ImVec2(32.0f, 32.0f));
	}

	if (selected) {
		ImGui::PopStyleColor();
	}

	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", tooltip);
	}

	if (clicked) {
		gizmoOperation_ = operation;
		return true;
	}
#else
	(void)id;
	(void)fallbackLabel;
	(void)textureIndex;
	(void)operation;
	(void)tooltip;
#endif // USE_IMGUI
	return false;
}

void SceneViewWindow::DrawGizmoToolbar() {
#ifdef USE_IMGUI
	LoadGizmoIcons();

	DrawGizmoModeButton("##GizmoTranslate", "T", gizmoTranslateIconIndex_, TransformGizmoOperation::Translate, "Translate");
	ImGui::SameLine();
	DrawGizmoModeButton("##GizmoRotate", "R", gizmoRotateIconIndex_, TransformGizmoOperation::Rotate, "Rotate");
	ImGui::SameLine();
	DrawGizmoModeButton("##GizmoScale", "S", gizmoScaleIconIndex_, TransformGizmoOperation::Scale, "Scale");

	// UI編集モードのトグル。ON中はScene RTにマウス操作でUIを移動/リサイズ/整列できる。
	ImGui::SameLine(0.0f, 20.0f);
	{
		const bool on = uiEditMode_;
		if (on) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		if (ImGui::Button("UI Edit", ImVec2(0.0f, 32.0f))) {
			uiEditMode_ = !uiEditMode_;
			if (uiEditMode_) {
				// UI編集開始時、UI関連が未選択なら編集可能なUI要素(Canvas直下の子=RectTransform持ち)を選ぶ。
				// 子が無ければCanvas自身を選ぶ(枠は見えるが編集は不可)。
				EditorSelection* selection = EditorSelection::GetInstance();
				if (!IsUIRelatedObject(selection->GetSelectedGameObject())) {
					if (Scene* scene = EditorApplication::GetInstance()->GetCurrentScene()) {
						GameObject* canvasFallback = nullptr;
						GameObject* editableChild = nullptr;
						for (const std::unique_ptr<GameObject>& gameObject : scene->GetGameObjects()) {
							if (!gameObject || !gameObject->GetComponent<CanvasComponent>()) {
								continue;
							}
							if (!canvasFallback) {
								canvasFallback = gameObject.get();
							}
							for (GameObject* child : gameObject->GetChildren()) {
								if (child && child->GetComponent<RectTransformComponent>()) {
									editableChild = child;
									break;
								}
							}
							if (editableChild) {
								break;
							}
						}
						if (editableChild) {
							selection->SetSelectedGameObject(editableChild);
						} else if (canvasFallback) {
							selection->SetSelectedGameObject(canvasFallback);
						}
					}
				}
			}
			uiDrag_ = UIEditDrag::None;
			uiDragUndoCaptured_ = false;
		}
		if (on) {
			ImGui::PopStyleColor();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("UI Edit Mode: drag to move/resize UI, snaps to guides (PowerPoint-like)");
		}
	}
#endif // USE_IMGUI
}

void SceneViewWindow::DrawTransformGizmo(const ImVec2& imagePosition, const ImVec2& imageSize) {
#ifdef USE_IMGUI
	Scene* scene = EditorApplication::GetInstance()->GetCurrentScene();
	if (!scene) {
		return;
	}

	Camera* camera = scene->GetEditorCamera();
	if (!camera) {
		return;
	}

	GameObject* selectedObject = EditorSelection::GetInstance()->GetSelectedGameObject();
	if (!selectedObject || !selectedObject->IsActive()) {
		return;
	}

	if (EditorApplication::GetInstance()->IsPlaying()) {
		return;
	}

	WorldTransform& transform = selectedObject->GetTransform();
	selectedObject->UpdateWorldTransformSelfAndAncestors();
	Matrix4x4 gizmoMatrix = transform.matWorld_;

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
	ImGuizmo::SetRect(imagePosition.x, imagePosition.y, imageSize.x, imageSize.y);

	ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
	if (gizmoOperation_ == TransformGizmoOperation::Rotate) {
		operation = ImGuizmo::ROTATE;
	} else if (gizmoOperation_ == TransformGizmoOperation::Scale) {
		operation = ImGuizmo::SCALE;
	}

	// ギズモのサイズ変更
	ImGuizmo::SetGizmoSizeClipSpace(0.2f);

	if (!ImGuizmo::Manipulate(MatrixData(camera->matView), MatrixData(camera->matProjection), operation, ImGuizmo::LOCAL, MatrixData(gizmoMatrix))) {
		if (!ImGuizmo::IsUsing()) {
			transformGizmoUsing_ = false;
		}
		return;
	}

	if (!transformGizmoUsing_) {
		CaptureUndo(*scene, "Transform Gizmo");
		transformGizmoUsing_ = true;
	}

	float translation[3]{};
	float rotationDegrees[3]{};
	float scale[3]{};
	Matrix4x4 localGizmoMatrix = gizmoMatrix;
	if (transform.parent_) {
		localGizmoMatrix = gizmoMatrix * Inverse(transform.parent_->matWorld_);
	}

	ImGuizmo::DecomposeMatrixToComponents(MatrixData(localGizmoMatrix), translation, rotationDegrees, scale);

	transform.translation_ = {translation[0], translation[1], translation[2]};
	transform.rotation_ = {
	    rotationDegrees[0] * kDegreesToRadians,
	    rotationDegrees[1] * kDegreesToRadians,
	    rotationDegrees[2] * kDegreesToRadians,
	};
	transform.scale_ = {scale[0], scale[1], scale[2]};

	if (!std::isfinite(transform.scale_.x) || transform.scale_.x < 0.001f) {
		transform.scale_.x = 0.001f;
	}
	if (!std::isfinite(transform.scale_.y) || transform.scale_.y < 0.001f) {
		transform.scale_.y = 0.001f;
	}
	if (!std::isfinite(transform.scale_.z) || transform.scale_.z < 0.001f) {
		transform.scale_.z = 0.001f;
	}
	if (!ImGuizmo::IsUsing()) {
		transformGizmoUsing_ = false;
	}
#else
	(void)imagePosition;
	(void)imageSize;
#endif // USE_IMGUI
}

void SceneViewWindow::HandleGameWindowObjectSelection(const ImVec2& imagePosition, const ImVec2& imageSize) {
#ifdef USE_IMGUI
	if (EditorApplication::GetInstance()->IsPlaying()) {
		return;
	}
	if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
		return;
	}
	if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		return;
	}
	if (ImGuizmo::IsUsing() || ImGuizmo::IsOver()) {
		return;
	}

	ImVec2 mousePosition = ImGui::GetIO().MousePos;
	if (mousePosition.x < imagePosition.x) {
		return;
	}
	if (mousePosition.y < imagePosition.y) {
		return;
	}
	if (mousePosition.x > imagePosition.x + imageSize.x) {
		return;
	}
	if (mousePosition.y > imagePosition.y + imageSize.y) {
		return;
	}

	Scene* scene = EditorApplication::GetInstance()->GetCurrentScene();
	if (!scene) {
		return;
	}

	Camera* camera = scene->GetEditorCamera();
	if (!camera) {
		return;
	}

	Ray ray{};
	Vector2 point = {mousePosition.x, mousePosition.y};
	Vector2 viewportPosition = {imagePosition.x, imagePosition.y};
	Vector2 viewportSize = {imageSize.x, imageSize.y};
	if (!RayCast::CreateRayFromViewportPoint(point, viewportPosition, viewportSize, *camera, ray)) {
		return;
	}

	RayCastHit hit{};
	if (RayCast::CastForEditor(*scene, ray, hit)) {
		if (hit.gameObject) {
			EditorSelection::GetInstance()->SetSelectedGameObject(hit.gameObject);
		}
	} else {
		EditorSelection::GetInstance()->Clear();
	}
#else
	(void)imagePosition;
	(void)imageSize;
#endif // USE_IMGUI
}

void SceneViewWindow::Draw(bool* pOpen) {
#ifdef USE_IMGUI
	// Begin の戻り値で可視性(タブ非アクティブ/折り畳み時はfalse)を判定し、非表示ならパスをスキップさせる。
	bool visible = ImGui::Begin("Scene", pOpen);
	SetSceneViewVisible(visible);

	// Sceneビューがフォーカスされている間だけデバッグカメラを操作できるようにする(プレイ中も含む)。
	// これによりGameビュー(メインカメラ)やゲーム入力と操作が競合しない。
	SetSceneViewFocused(visible && ImGui::IsWindowFocused());

	// UI編集モード中はSceneビューにCanvasのUIを常に描画させる(選択に依存しない)。
	SetSceneViewUIEditMode(visible && uiEditMode_);

	if (!visible) {
		ImGui::End();
		return;
	}

	DrawGizmoToolbar();
	ImGui::Separator();

	// DockされたGameウィンドウの内側サイズを取得する。タブや枠の分を除いた描画可能領域。
	ImVec2 contentSize = ImGui::GetContentRegionAvail();
	// Imageに0以下のサイズを渡さないように最低サイズを保証する。
	if (contentSize.x < 1.0f) {
		contentSize.x = 1.0f;
	}
	if (contentSize.y < 1.0f) {
		contentSize.y = 1.0f;
	}

	// Sceneビューの、ポストプロセス(ブルーム/トーンマップ)適用済みSRVをImGuiへ渡す。
	// Scene RTはGameビューと同じく固定解像度(1280x720)。表示側でアスペクト比を保ってレターボックス表示する。
	// (表示サイズ追従は毎フレームのGPU待ち/作り直しで不安定だったため固定に戻した)
	// 描画本体はEditorApplicationでBeginSceneRenderからEndSceneRender+PostProcess::Renderで行われる。
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	D3D12_GPU_DESCRIPTOR_HANDLE handle = PostProcess::GetInstance()->GetDisplaySrvHandle(DirectXCommon::kSceneViewIndex);

	float gameAspect = 16.0f / 9.0f;
	if (dxCommon->GetSceneRenderHeight() > 0) {
		gameAspect = static_cast<float>(dxCommon->GetSceneRenderWidth()) / static_cast<float>(dxCommon->GetSceneRenderHeight());
	}

	ImVec2 imageSize = contentSize;
	ImVec2 imageOffset = {0.0f, 0.0f};
	float contentAspect = contentSize.x / contentSize.y;
	if (contentAspect > gameAspect) {
		imageSize.y = contentSize.y;
		imageSize.x = imageSize.y * gameAspect;
		imageOffset.x = (contentSize.x - imageSize.x) * 0.5f;
	} else {
		imageSize.x = contentSize.x;
		imageSize.y = imageSize.x / gameAspect;
		imageOffset.y = (contentSize.y - imageSize.y) * 0.5f;
	}

	ImVec2 contentPosition = ImGui::GetCursorScreenPos();
	ImVec2 contentEnd = {contentPosition.x + contentSize.x, contentPosition.y + contentSize.y};
	ImVec2 imagePosition = {contentPosition.x + imageOffset.x, contentPosition.y + imageOffset.y};
	ImVec2 imageEnd = {imagePosition.x + imageSize.x, imagePosition.y + imageSize.y};

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(contentPosition, contentEnd, IM_COL32(0, 0, 0, 255));
	drawList->AddImage(static_cast<ImTextureID>(handle.ptr), imagePosition, imageEnd, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
	ImGui::Dummy(contentSize);

	if (uiEditMode_) {
		// UI編集モード: 3Dギズモ/3Dピックの代わりに、マウスでUIを移動/リサイズ/整列する。
		DrawUIEditView(imagePosition, imageSize);
	} else {
		DrawTransformGizmo(imagePosition, imageSize);
		HandleGameWindowObjectSelection(imagePosition, imageSize);

		// 選択中UI要素(RectTransform)の矩形をSceneビューにアウトライン表示する。
		GameObject* selectedObject = EditorSelection::GetInstance()->GetSelectedGameObject();
		if (selectedObject) {
			DirectXCommon* dxCommon = DirectXCommon::GetInstance();
			const float sceneWidth = static_cast<float>(dxCommon->GetSceneRenderWidth());
			const float sceneHeight = static_cast<float>(dxCommon->GetSceneRenderHeight());
			UIRect elementRect;
			float scaleFactor = 1.0f;
			if (sceneWidth > 0.0f && sceneHeight > 0.0f && ComputeUIElementRect(selectedObject, sceneWidth, sceneHeight, elementRect, scaleFactor)) {
				const float pixelX = elementRect.x * scaleFactor;
				const float pixelY = elementRect.y * scaleFactor;
				const float pixelW = elementRect.width * scaleFactor;
				const float pixelH = elementRect.height * scaleFactor;
				const ImVec2 topLeft{imagePosition.x + (pixelX / sceneWidth) * imageSize.x, imagePosition.y + (pixelY / sceneHeight) * imageSize.y};
				const ImVec2 bottomRight{imagePosition.x + ((pixelX + pixelW) / sceneWidth) * imageSize.x, imagePosition.y + ((pixelY + pixelH) / sceneHeight) * imageSize.y};
				drawList->AddRect(topLeft, bottomRight, IM_COL32(80, 200, 255, 255), 0.0f, 0, 2.0f);
			}
		}
	}

	ImGui::End();
#else
	(void)pOpen;
#endif // USE_IMGUI
}

void SceneViewWindow::DrawUIEditView(const ImVec2& imagePosition, const ImVec2& imageSize) {
#ifdef USE_IMGUI
	Scene* scene = EditorApplication::GetInstance()->GetCurrentScene();
	if (!scene || EditorApplication::GetInstance()->IsPlaying()) {
		return;
	}

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	const float sceneW = static_cast<float>(dxCommon->GetSceneRenderWidth());
	const float sceneH = static_cast<float>(dxCommon->GetSceneRenderHeight());
	if (sceneW <= 0.0f || sceneH <= 0.0f || imageSize.x <= 0.0f || imageSize.y <= 0.0f) {
		return;
	}

	ImDrawList* dl = ImGui::GetWindowDrawList();

	// RTピクセル座標 ⇔ スクリーン(画像)座標の変換。
	auto toScreen = [&](float px, float py) -> ImVec2 {
		return ImVec2(imagePosition.x + (px / sceneW) * imageSize.x, imagePosition.y + (py / sceneH) * imageSize.y);
	};
	auto toPixelX = [&](float sx) { return (sx - imagePosition.x) / imageSize.x * sceneW; };
	auto toPixelY = [&](float sy) { return (sy - imagePosition.y) / imageSize.y * sceneH; };

	// 全UI要素(描画順)を集める。
	std::vector<UIElementRectInfo> elements;
	CollectSceneUIElementRects(*scene, sceneW, sceneH, elements);

	// 全要素を薄い枠でうっすら見せる(整列の目安)。
	for (const UIElementRectInfo& e : elements) {
		const ImVec2 a = toScreen(e.pixelRect.x, e.pixelRect.y);
		const ImVec2 b = toScreen(e.pixelRect.x + e.pixelRect.width, e.pixelRect.y + e.pixelRect.height);
		dl->AddRect(a, b, IM_COL32(140, 140, 140, 90), 0.0f, 0, 1.0f);
	}

	EditorSelection* selection = EditorSelection::GetInstance();
	GameObject* selected = selection->GetSelectedGameObject();
	RectTransformComponent* selRt = selected ? selected->GetComponent<RectTransformComponent>() : nullptr;

	// 選択要素のピクセル矩形とスケール(キャンバス→ピクセル倍率)を求める。
	UIRect selPixel{};
	float selScale = 1.0f;
	bool editable = false;
	if (selRt) {
		UIRect r{};
		float sf = 1.0f;
		if (ComputeUIElementRect(selected, sceneW, sceneH, r, sf)) {
			selPixel = {r.x * sf, r.y * sf, r.width * sf, r.height * sf};
			selScale = sf;
			editable = true;
		}
	}

	// 親矩形(キャンバス単位)。逆算に使う。親がCanvasでも取得できる。
	UIRect parentRectCanvas{0.0f, 0.0f, sceneW, sceneH};
	if (editable) {
		GameObject* parent = selected->GetParent();
		UIRect pr{};
		float psf = 1.0f;
		if (parent && ComputeUIElementRect(parent, sceneW, sceneH, pr, psf)) {
			parentRectCanvas = pr;
		} else {
			parentRectCanvas = {0.0f, 0.0f, sceneW / selScale, sceneH / selScale};
		}
	}

	// 整列ガイド候補(RTピクセル空間): キャンバスの端/中央 + 自身以外の要素の端/中央。
	std::vector<float> vGuides; // 縦線(x)
	std::vector<float> hGuides; // 横線(y)
	vGuides.push_back(0.0f);
	vGuides.push_back(sceneW * 0.5f);
	vGuides.push_back(sceneW);
	hGuides.push_back(0.0f);
	hGuides.push_back(sceneH * 0.5f);
	hGuides.push_back(sceneH);

	auto isSelfOrDescendant = [&](GameObject* o) {
		for (GameObject* c = o; c != nullptr; c = c->GetParent()) {
			if (c == selected) {
				return true;
			}
		}
		return false;
	};
	if (editable) {
		for (const UIElementRectInfo& e : elements) {
			if (isSelfOrDescendant(e.object)) {
				continue;
			}
			const float x0 = e.pixelRect.x;
			const float x1 = e.pixelRect.x + e.pixelRect.width;
			const float y0 = e.pixelRect.y;
			const float y1 = e.pixelRect.y + e.pixelRect.height;
			vGuides.push_back(x0);
			vGuides.push_back((x0 + x1) * 0.5f);
			vGuides.push_back(x1);
			hGuides.push_back(y0);
			hGuides.push_back((y0 + y1) * 0.5f);
			hGuides.push_back(y1);
		}
	}

	// スナップ閾値(スクリーン8px相当をRTピクセルへ換算)。
	const float snapPxX = 8.0f * sceneW / imageSize.x;
	const float snapPxY = 8.0f * sceneH / imageSize.y;

	// ハンドル定義(3x3から中央を除いた8点)。nx,nyは矩形内の正規化位置。
	struct HandleDef {
		float nx;
		float ny;
		UIEditDrag drag;
	};
	const HandleDef kHandles[8] = {
	    {0.0f, 0.0f, UIEditDrag::ResizeTL}, {0.5f, 0.0f, UIEditDrag::ResizeT}, {1.0f, 0.0f, UIEditDrag::ResizeTR},
	    {1.0f, 0.5f, UIEditDrag::ResizeR}, {1.0f, 1.0f, UIEditDrag::ResizeBR}, {0.5f, 1.0f, UIEditDrag::ResizeB},
	    {0.0f, 1.0f, UIEditDrag::ResizeBL}, {0.0f, 0.5f, UIEditDrag::ResizeL},
	};
	auto handleScreen = [&](float nx, float ny) { return toScreen(selPixel.x + nx * selPixel.width, selPixel.y + ny * selPixel.height); };

	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const bool insideImage = mouse.x >= imagePosition.x && mouse.y >= imagePosition.y && mouse.x <= imagePosition.x + imageSize.x && mouse.y <= imagePosition.y + imageSize.y;
	const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	const float kHandleHit = 7.0f; // スクリーンpx

	// マウス押下でドラッグ種別を決定(ハンドル→リサイズ、本体→移動、外→別要素を選択)。
	if (uiDrag_ == UIEditDrag::None && hovered && insideImage && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		UIEditDrag hit = UIEditDrag::None;
		if (editable) {
			for (const HandleDef& h : kHandles) {
				const ImVec2 c = handleScreen(h.nx, h.ny);
				if (std::fabs(mouse.x - c.x) <= kHandleHit && std::fabs(mouse.y - c.y) <= kHandleHit) {
					hit = h.drag;
					break;
				}
			}
			if (hit == UIEditDrag::None) {
				const ImVec2 a = toScreen(selPixel.x, selPixel.y);
				const ImVec2 b = toScreen(selPixel.x + selPixel.width, selPixel.y + selPixel.height);
				if (mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y) {
					hit = UIEditDrag::Move;
				}
			}
		}

		if (hit != UIEditDrag::None) {
			uiDrag_ = hit;
			uiDragStartPixelRect_ = selPixel;
			uiDragStartMousePixel_ = ImVec2(toPixelX(mouse.x), toPixelY(mouse.y));
			uiDragUndoCaptured_ = false;
		} else {
			// 別のUI要素を選択(描画順で後=手前を優先)。何も無ければ選択解除。
			const float mpx = toPixelX(mouse.x);
			const float mpy = toPixelY(mouse.y);
			GameObject* picked = nullptr;
			for (const UIElementRectInfo& e : elements) {
				if (mpx >= e.pixelRect.x && mpx <= e.pixelRect.x + e.pixelRect.width && mpy >= e.pixelRect.y && mpy <= e.pixelRect.y + e.pixelRect.height) {
					picked = e.object;
				}
			}
			if (picked) {
				selection->SetSelectedGameObject(picked);
			} else {
				selection->Clear();
			}
		}
	}

	// ドラッグ中の更新。
	UIRect drawRect = selPixel;
	bool hasVGuide = false;
	bool hasHGuide = false;
	float activeVGuide = 0.0f;
	float activeHGuide = 0.0f;

	if (uiDrag_ != UIEditDrag::None && editable) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			if (!uiDragUndoCaptured_) {
				CaptureUndo(*scene, "UI Edit");
				uiDragUndoCaptured_ = true;
			}

			const float curX = toPixelX(mouse.x);
			const float curY = toPixelY(mouse.y);
			const float dx = curX - uiDragStartMousePixel_.x;
			const float dy = curY - uiDragStartMousePixel_.y;

			const UIRect s = uiDragStartPixelRect_;
			const bool move = uiDrag_ == UIEditDrag::Move;
			const bool left = uiDrag_ == UIEditDrag::ResizeTL || uiDrag_ == UIEditDrag::ResizeL || uiDrag_ == UIEditDrag::ResizeBL;
			const bool right = uiDrag_ == UIEditDrag::ResizeTR || uiDrag_ == UIEditDrag::ResizeR || uiDrag_ == UIEditDrag::ResizeBR;
			const bool top = uiDrag_ == UIEditDrag::ResizeTL || uiDrag_ == UIEditDrag::ResizeT || uiDrag_ == UIEditDrag::ResizeTR;
			const bool bottom = uiDrag_ == UIEditDrag::ResizeBL || uiDrag_ == UIEditDrag::ResizeB || uiDrag_ == UIEditDrag::ResizeBR;

			float nx = s.x;
			float ny = s.y;
			float nw = s.width;
			float nh = s.height;
			if (move) {
				nx = s.x + dx;
				ny = s.y + dy;
			}
			if (left) {
				nx = s.x + dx;
				nw = s.width - dx;
			}
			if (right) {
				nw = s.width + dx;
			}
			if (top) {
				ny = s.y + dy;
				nh = s.height - dy;
			}
			if (bottom) {
				nh = s.height + dy;
			}

			// スナップ(整列ガイド)。移動は3辺(左/中央/右)の最良、リサイズは動かす辺のみ。
			if (move) {
				float bestDiff = snapPxX;
				bool got = false;
				float guide = 0.0f;
				const float ex[3] = {nx, nx + nw * 0.5f, nx + nw};
				for (float e : ex) {
					for (float g : vGuides) {
						const float d = e - g;
						if (std::fabs(d) <= std::fabs(bestDiff)) {
							bestDiff = d;
							got = true;
							guide = g;
						}
					}
				}
				if (got) {
					nx -= bestDiff;
					hasVGuide = true;
					activeVGuide = guide;
				}
				float bestDiffY = snapPxY;
				bool gotY = false;
				float guideY = 0.0f;
				const float ey[3] = {ny, ny + nh * 0.5f, ny + nh};
				for (float e : ey) {
					for (float g : hGuides) {
						const float d = e - g;
						if (std::fabs(d) <= std::fabs(bestDiffY)) {
							bestDiffY = d;
							gotY = true;
							guideY = g;
						}
					}
				}
				if (gotY) {
					ny -= bestDiffY;
					hasHGuide = true;
					activeHGuide = guideY;
				}
			} else {
				if (left) {
					bool sn = false;
					float g = 0.0f;
					const float v = SnapToGuides(nx, vGuides, snapPxX, sn, g);
					if (sn) {
						nw += nx - v;
						nx = v;
						hasVGuide = true;
						activeVGuide = g;
					}
				} else if (right) {
					bool sn = false;
					float g = 0.0f;
					const float v = SnapToGuides(nx + nw, vGuides, snapPxX, sn, g);
					if (sn) {
						nw = v - nx;
						hasVGuide = true;
						activeVGuide = g;
					}
				}
				if (top) {
					bool sn = false;
					float g = 0.0f;
					const float v = SnapToGuides(ny, hGuides, snapPxY, sn, g);
					if (sn) {
						nh += ny - v;
						ny = v;
						hasHGuide = true;
						activeHGuide = g;
					}
				} else if (bottom) {
					bool sn = false;
					float g = 0.0f;
					const float v = SnapToGuides(ny + nh, hGuides, snapPxY, sn, g);
					if (sn) {
						nh = v - ny;
						hasHGuide = true;
						activeHGuide = g;
					}
				}
			}

			// 最小サイズを保証(反対側の辺を固定)。
			const float kMin = 1.0f;
			if (nw < kMin) {
				if (left) {
					nx = s.x + s.width - kMin;
				}
				nw = kMin;
			}
			if (nh < kMin) {
				if (top) {
					ny = s.y + s.height - kMin;
				}
				nh = kMin;
			}

			drawRect = {nx, ny, nw, nh};

			// ピクセル→キャンバス単位に戻してRectTransformへ反映。
			const UIRect targetCanvas{nx / selScale, ny / selScale, nw / selScale, nh / selScale};
			ApplyRectToRectTransform(selRt, parentRectCanvas, targetCanvas);
		} else {
			uiDrag_ = UIEditDrag::None;
			uiDragUndoCaptured_ = false;
		}
	}

	// 整列ガイド線を描画(スナップ中のみ)。
	if (hasVGuide) {
		const ImVec2 p0 = toScreen(activeVGuide, 0.0f);
		dl->AddLine(ImVec2(p0.x, imagePosition.y), ImVec2(p0.x, imagePosition.y + imageSize.y), IM_COL32(255, 80, 200, 220), 1.0f);
	}
	if (hasHGuide) {
		const ImVec2 p0 = toScreen(0.0f, activeHGuide);
		dl->AddLine(ImVec2(imagePosition.x, p0.y), ImVec2(imagePosition.x + imageSize.x, p0.y), IM_COL32(255, 80, 200, 220), 1.0f);
	}

	// 選択要素の枠 + 8ハンドルを描画。
	if (editable) {
		const ImVec2 a = toScreen(drawRect.x, drawRect.y);
		const ImVec2 b = toScreen(drawRect.x + drawRect.width, drawRect.y + drawRect.height);
		dl->AddRect(a, b, IM_COL32(80, 200, 255, 255), 0.0f, 0, 2.0f);
		for (const HandleDef& h : kHandles) {
			const ImVec2 c = toScreen(drawRect.x + h.nx * drawRect.width, drawRect.y + h.ny * drawRect.height);
			dl->AddRectFilled(ImVec2(c.x - 4.0f, c.y - 4.0f), ImVec2(c.x + 4.0f, c.y + 4.0f), IM_COL32(255, 255, 255, 255));
			dl->AddRect(ImVec2(c.x - 4.0f, c.y - 4.0f), ImVec2(c.x + 4.0f, c.y + 4.0f), IM_COL32(40, 40, 40, 255));
		}
	}

	// 操作ヒント。
	const char* hint = editable ? "UI Edit: drag body to move, handles to resize. Click to select." : "UI Edit: select a UI element (under a Canvas).";
	dl->AddText(ImVec2(imagePosition.x + 8.0f, imagePosition.y + 6.0f), IM_COL32(255, 255, 255, 220), hint);
#else
	(void)imagePosition;
	(void)imageSize;
#endif // USE_IMGUI
}

} // namespace KujataEngine
