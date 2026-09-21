#include "EditorBillboards.h"
#include "IEditorBillboard.h"
#include "ISceneCamera.h"
#include "Scene.h"
#include "../3d/Camera.h"
#include "../3d/Model.h"
#include "../3d/WorldTransform.h"
#include "../base/ProjectPath.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace KujataEngine {

namespace {

constexpr float kEditorBillboardScale = 0.45f;

struct EditorBillboardDrawCache {
	std::unordered_map<std::string, std::unique_ptr<Model>> models;
	std::unordered_map<const Component*, std::unique_ptr<WorldTransform>> transforms;
};

EditorBillboardDrawCache& GetEditorBillboardDrawCache() {
	static EditorBillboardDrawCache cache;
	return cache;
}

Model* GetOrCreateEditorBillboardModel(const std::string& iconName) {
	if (iconName.empty()) {
		return nullptr;
	}

	EditorBillboardDrawCache& cache = GetEditorBillboardDrawCache();
	auto found = cache.models.find(iconName);
	if (found != cache.models.end()) {
		return found->second.get();
	}

	// Editor用アイコンも通常の3Dモデルと同じTexture/Model経路で扱い、GameWindow内の実体あるPlaneとして描画する。
	std::string texturePath = (GetEditorIconDirectory() / iconName).string();
	std::unique_ptr<Model> model(Model::CreatePlane(texturePath, ShaderModel::kNone));
	Model* rawModel = model.get();
	cache.models.emplace(iconName, std::move(model));
	return rawModel;
}

Model* FindEditorBillboardModel(const std::string& iconName) {
	EditorBillboardDrawCache& cache = GetEditorBillboardDrawCache();
	auto found = cache.models.find(iconName);
	if (found == cache.models.end()) {
		return nullptr;
	}

	return found->second.get();
}

WorldTransform* GetOrCreateEditorBillboardTransform(const Component* component) {
	if (!component) {
		return nullptr;
	}

	EditorBillboardDrawCache& cache = GetEditorBillboardDrawCache();
	auto found = cache.transforms.find(component);
	if (found != cache.transforms.end()) {
		return found->second.get();
	}

	std::unique_ptr<WorldTransform> transform = std::make_unique<WorldTransform>();
	transform->Initialize();
	WorldTransform* rawTransform = transform.get();
	cache.transforms.emplace(component, std::move(transform));
	return rawTransform;
}

WorldTransform* FindEditorBillboardTransform(const Component* component) {
	EditorBillboardDrawCache& cache = GetEditorBillboardDrawCache();
	auto found = cache.transforms.find(component);
	if (found == cache.transforms.end()) {
		return nullptr;
	}

	return found->second.get();
}

} // namespace

void EditorBillboards::PrepareComponent(const Component* component) {
#ifdef USE_IMGUI
	const IEditorBillboard* billboard = dynamic_cast<const IEditorBillboard*>(component);
	if (!billboard) {
		return;
	}

	// TextureManager::LoadTextureはCommandListを実行するため、描画中ではなく初期化・追加時にPlaneを作る。
	GetOrCreateEditorBillboardModel(billboard->GetEditorBillboardIconName());
	GetOrCreateEditorBillboardTransform(component);
#else
	// エディタUI無し(ゲーム単体)ビルドではEditorカメラが無く、DrawEditorBillboardsは常に描画しない。
	// アイコン画像(KujataEngine/resources/images)も配布物に含めないため、準備自体を行わない。
	(void)component;
#endif // USE_IMGUI
}

void EditorBillboards::PrepareScene(Scene& scene) {
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject) {
			continue;
		}

		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			PrepareComponent(component.get());
		}
	}
}

void EditorBillboards::Draw(Scene& scene) {
	// 出し分けは呼び出し側(RenderViewのdrawEditorOverlays)が担当する。
	Camera* camera = scene.GetEditorCamera();
	if (!camera) {
		return;
	}

	std::unordered_set<const Component*> activeBillboardComponents;
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}

		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			if (!component || !component->IsEnabled()) {
				continue;
			}
			const IEditorBillboard* billboard = dynamic_cast<const IEditorBillboard*>(component.get());
			if (!billboard) {
				continue;
			}
			const ISceneCamera* sceneCamera = dynamic_cast<const ISceneCamera*>(component.get());
			if (sceneCamera && sceneCamera->GetSceneCamera() == camera) {
				// 表示に使っているCamera自身のアイコンを描くと、視点原点のPlaneが目前でちらつくため描画しない。
				continue;
			}

			// Draw中にテクスチャロードが走るとCommandListの状態が壊れるため、準備済みのPlaneだけ描画する。
			Model* model = FindEditorBillboardModel(billboard->GetEditorBillboardIconName());
			WorldTransform* transform = FindEditorBillboardTransform(component.get());
			if (!model || !transform) {
				continue;
			}

			activeBillboardComponents.insert(component.get());

			// GameObjectのワールド位置へPlaneを置き、WorldTransform側のBillboard行列で常にEditorCameraへ向ける。
			transform->translation_ = gameObject->GetTransform().GetWorldPosition();
			transform->rotation_ = {0.0f, 0.0f, 0.0f};
			transform->scale_ = {kEditorBillboardScale, kEditorBillboardScale, kEditorBillboardScale};
			transform->UpdateMatrix(*camera, true);
			model->Draw(*transform, *camera);
		}
	}

	EditorBillboardDrawCache& cache = GetEditorBillboardDrawCache();
	for (auto iterator = cache.transforms.begin(); iterator != cache.transforms.end();) {
		if (activeBillboardComponents.find(iterator->first) == activeBillboardComponents.end()) {
			iterator = cache.transforms.erase(iterator);
		} else {
			++iterator;
		}
	}
}

void EditorBillboards::ClearCache() {
	EditorBillboardDrawCache& cache = GetEditorBillboardDrawCache();
	cache.transforms.clear();
	cache.models.clear();
}

} // namespace KujataEngine
