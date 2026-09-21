// Sceneの描画まわり(影パス・Volume適用・ビュー描画・スクリーン空間UI)。
// 何を描くかの中身はGameObject/Component・SceneGizmos・EditorBillboards・UIレンダラーにあり、
// ここは描く順番とビューごとの出し分けだけを決める。
#include "Scene.h"
#include "EditorBillboards.h"
#include "SceneGizmos.h"
#include "../3d/Camera.h"
#include "../3d/DirectionalLight.h"
#include "../2d/Sprite2DRenderer.h"
#include "../2d/UICanvasRenderer.h"
#include "../base/DirectXCommon.h"
#include "../components/ModelRendererComponent.h"
#include "../input/Input.h"
#include "../postprocess/PostProcess.h"
#include "../postprocess/VolumeStack.h"
#include "../runtime/PlayState.h"
#include "../runtime/SelectionProvider.h"
#include "../shadow/ShadowMap.h"

namespace KujataEngine {

void Scene::RefreshEditorBillboards() { EditorBillboards::PrepareScene(*this); }

void Scene::Draw() {
	UpdateWorldTransforms();

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (gameObject && gameObject->IsRoot()) {
			gameObject->DrawHierarchy();
		}
	}

	EditorBillboards::Draw(*this);
	SceneGizmos::DrawSelectedColliders(*this);
	SceneGizmos::DrawSelectedCameraFrustum(*this);
	SceneGizmos::DrawGrid(Vector4(0.4f, 0.4f, 0.4f, 1.0f));
}

void Scene::PrepareFrame() { UpdateWorldTransforms(); }

void Scene::RenderShadowPass() {
	ShadowMap* shadowMap = ShadowMap::GetInstance();
	if (!shadowMap->IsInitialized()) {
		return;
	}

	// ライトの向きは現在のDirectionalLight(=シーン上のDirectionalLightComponentが毎フレーム反映済み)。
	const Vector3& lightDirection = DirectionalLight::GetInstance()->GetData().direction;
	// TODO: focusPositionをプレイヤー/カメラ注視点へ追従させると、広いシーンでも影の解像度を保てる。
	shadowMap->UpdateLightMatrix(lightDirection, {0.0f, 0.0f, 0.0f}, 40.0f, 100.0f);

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	const uint32_t previousViewIndex = dxCommon->GetRenderViewIndex();
	// ライト視点のWVPをScene/Gameとは別の定数バッファ枠へ書くため、ここで切り替える。
	dxCommon->SetRenderViewIndex(DirectXCommon::kShadowViewIndex);

	shadowMap->BeginWrite();
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			if (!component || !component->IsEnabled()) {
				continue;
			}
			ModelRendererComponent* renderer = component->AsModelRendererComponent();
			// **Cast Shadowを切ったものは影パスで描かない。**
			// 小さなパーツを大量に置くと、個別の影は絵に効かないのにドローコールだけ倍になる。
			if (renderer && renderer->CastsShadow()) {
				renderer->DrawShadow(shadowMap->GetLightViewProjection());
			}
		}
	}
	shadowMap->EndWrite();

	dxCommon->SetRenderViewIndex(previousViewIndex);
}

void Scene::ApplyVolumes(const Camera* camera) {
	// カメラが無いビュー(まだシーンカメラが揃っていない等)は原点基準で解決しておく。
	// Global Volumeしか無いシーンではカメラ位置は結果に影響しない。
	Vector3 cameraPosition = camera ? camera->translation_ : Vector3{0.0f, 0.0f, 0.0f};
	VolumeResolveResult resolved = VolumeStack::Resolve(*this, cameraPosition);
	PostProcess::GetInstance()->SetActiveProfile(resolved.profile);
}

void Scene::RenderView(Camera* camera, bool drawEditorOverlays) {
	(void)camera; // モデルのカメラは派生側でApplyRenderCameraToModelRenderers済みの想定。

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (gameObject && gameObject->IsRoot()) {
			gameObject->DrawHierarchy();
		}
	}

	// 編集用オーバーレイはSceneビューのみ(呼び出し側のdrawEditorOverlaysで制御)。
	if (drawEditorOverlays) {
		EditorBillboards::Draw(*this);
		SceneGizmos::DrawSelectedColliders(*this);
		SceneGizmos::DrawSelectedCameraFrustum(*this);
		SceneGizmos::DrawSelectedLightRanges(*this);
		SceneGizmos::DrawGrid(Vector4(0.4f, 0.4f, 0.4f, 1.0f));
	}

	// 全Collider可視化モードがONなら、Sceneビュー/Gameビュー問わず全Colliderを描く。
	// **F1の受け付けはここで行う。**
	// Scene::Update は Play 中しか回らないので、Update側にトグルを置くと
	// 「止めて配置を直している最中に全部を見る」ことができない(調整したい時ほど使えない)。
	if (drawEditorOverlays && Input::GetKeyTrigger(DIK_F1)) {
		ToggleShowAllColliders();
	}
	if (SceneGizmos::IsShowAllColliders()) {
		SceneGizmos::DrawAllColliders(*this);
	}

	// world空間2Dスプライト(Sprite方式)。3Dメッシュの後・スクリーン空間UIの前に、
	// Sorting Order順でまとめて描く(深度を書かないので描画順が前後関係になる)。
	DrawSceneSprites(*this, camera);

	// World Space Canvas。ワールドに置かれたUIなので、選択状態に関わらず常に描く。
	{
		DirectXCommon* dxCommon = DirectXCommon::GetInstance();
		DrawSceneWorldCanvases(*this, camera, static_cast<float>(dxCommon->GetGameRenderWidth()), static_cast<float>(dxCommon->GetGameRenderHeight()));
	}

	// Screen Space CanvasはここでHDRシーンRTへは描かない。フォグ/ブルーム/トーンマップの影響を
	// 受けないよう、ポストプロセス後のLDR RTへRenderScreenSpaceUIで描く。
}

void Scene::RenderScreenSpaceUI(float targetWidth, float targetHeight, bool drawEditorOverlays) {
	// drawEditorOverlays==true はSceneビュー、false はGameビュー。
	// Gameビューは常にUIを描く。SceneビューはUI(Canvasまたはその子孫)を選択中のときだけ描く。
	bool drawUI = !drawEditorOverlays;
	if (drawEditorOverlays) {
		// UI編集モード中は選択に関わらず常にCanvasを描く。それ以外はUI(またはその子孫)選択時のみ。
		drawUI = IsSceneViewUIEditMode() || IsUIRelatedObject(GetSelectionProvider().GetSelectedGameObject());
	}
	if (drawUI) {
		DrawSceneCanvases(*this, targetWidth, targetHeight);
	}
}

void Scene::SetShowAllColliders(bool show) { SceneGizmos::SetShowAllColliders(show); }

bool Scene::IsShowAllColliders() { return SceneGizmos::IsShowAllColliders(); }

void Scene::ToggleShowAllColliders() { SceneGizmos::SetShowAllColliders(!SceneGizmos::IsShowAllColliders()); }

} // namespace KujataEngine
