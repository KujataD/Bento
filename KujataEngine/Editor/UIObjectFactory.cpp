#include "UIObjectFactory.h"

#include "../components/CanvasComponent.h"
#include "../scene/Component.h"
#include "../scene/ComponentFactory.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include "EditorSelection.h"
#include <cstring>
#include <memory>
#include <string>

namespace KujataEngine {
namespace UIObjectFactory {
namespace {

bool HasCanvas(const GameObject& gameObject) {
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (component && std::strcmp(component->GetTypeName(), "Canvas") == 0) {
			return true;
		}
	}
	return false;
}

GameObject* FindFirstCanvas(Scene* scene) {
	for (const std::unique_ptr<GameObject>& gameObject : scene->GetGameObjects()) {
		if (gameObject && HasCanvas(*gameObject)) {
			return gameObject.get();
		}
	}
	return nullptr;
}

/// <summary>選択中のGameObjectから親を辿り、最初に見つかったCanvasを返す。</summary>
GameObject* FindSelectedCanvas() {
	GameObject* selected = EditorSelection::GetInstance()->GetSelectedGameObject();
	for (GameObject* current = selected; current != nullptr; current = current->GetParent()) {
		if (HasCanvas(*current)) {
			return current;
		}
	}
	return nullptr;
}

/// <summary>"Canvas", "Canvas (1)", ... のように既存と重複しない名前を作る。</summary>
std::string MakeUniqueName(Scene* scene, const std::string& baseName) {
	auto isUsed = [scene](const std::string& name) {
		for (const std::unique_ptr<GameObject>& gameObject : scene->GetGameObjects()) {
			if (gameObject && gameObject->GetName() == name) {
				return true;
			}
		}
		return false;
	};

	if (!isUsed(baseName)) {
		return baseName;
	}
	for (int suffix = 1; suffix < 1000; ++suffix) {
		std::string candidate = baseName + " (" + std::to_string(suffix) + ")";
		if (!isUsed(candidate)) {
			return candidate;
		}
	}
	return baseName;
}

void AddComponentByType(Scene* scene, GameObject* gameObject, const char* typeName) {
	Component* added = gameObject->AddComponent(ComponentFactory::GetInstance().Create(typeName));
	scene->OnEditorComponentAdded(gameObject, added);
}

} // namespace

GameObject* CreateCanvas(Scene* scene) {
	GameObject* canvasObject = scene->CreateGameObject(MakeUniqueName(scene, "Canvas"));
	AddComponentByType(scene, canvasObject, "Canvas");
	return canvasObject;
}

GameObject* CreateWorldSpaceCanvas(Scene* scene) {
	GameObject* canvasObject = scene->CreateGameObject(MakeUniqueName(scene, "World Canvas"));
	AddComponentByType(scene, canvasObject, "Canvas");

	CanvasComponent* canvas = canvasObject->GetComponent<CanvasComponent>();
	if (canvas) {
		canvas->SetRenderMode(CanvasComponent::RenderMode::WorldSpace);
	}

	// 既定の1280x720キャンバスがそのままだと巨大なので、Unityの慣例に合わせて0.01倍(=12.8x7.2 world単位)にする。
	canvasObject->GetTransform().scale_ = {0.01f, 0.01f, 0.01f};
	return canvasObject;
}

GameObject* EnsureCanvas(Scene* scene) {
	// Canvasが複数ある場合、今選択しているCanvasへ追加するのが自然(Unityと同じ)。
	if (GameObject* selectedCanvas = FindSelectedCanvas()) {
		return selectedCanvas;
	}
	if (GameObject* existing = FindFirstCanvas(scene)) {
		return existing;
	}
	return CreateCanvas(scene);
}

GameObject* CreateImage(Scene* scene) {
	GameObject* canvas = EnsureCanvas(scene);
	GameObject* image = scene->CreateGameObject("Image");
	AddComponentByType(scene, image, "RectTransform");
	AddComponentByType(scene, image, "Image");
	image->SetParent(canvas);
	return image;
}

GameObject* CreateText(Scene* scene) {
	GameObject* canvas = EnsureCanvas(scene);
	GameObject* text = scene->CreateGameObject("Text");
	AddComponentByType(scene, text, "RectTransform");
	AddComponentByType(scene, text, "Text");
	text->SetParent(canvas);
	return text;
}

GameObject* CreateButton(Scene* scene) {
	GameObject* canvas = EnsureCanvas(scene);
	GameObject* button = scene->CreateGameObject("Button");
	AddComponentByType(scene, button, "RectTransform");
	AddComponentByType(scene, button, "Image");
	AddComponentByType(scene, button, "Button");
	button->SetParent(canvas);

	GameObject* label = scene->CreateGameObject("Text");
	AddComponentByType(scene, label, "RectTransform");
	AddComponentByType(scene, label, "Text");
	label->SetParent(button);
	return button;
}

} // namespace UIObjectFactory
} // namespace KujataEngine
