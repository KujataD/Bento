#pragma once

#include "../runtime/KujataApi.h"
#include "Component.h"
#include "../3d/WorldTransform.h"
#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace KujataEngine {

class Scene;

/// <summary>
/// Scene上に配置するObjectの最小単位
/// </summary>
class GameObject {
public:
	KUJATA_API explicit GameObject(const std::string& name = "GameObject");
	KUJATA_API ~GameObject();


	KUJATA_API void Initialize();
	KUJATA_API void Update();
	KUJATA_API void Draw();

	KUJATA_API void Finalize();

	/// <summary>
	/// EditからPlayへ入る直前の処理
	/// </summary>
	KUJATA_API void OnPlayStart();

	/// <summary>
	/// PlayからEditへ戻る時の処理
	/// </summary>
	KUJATA_API void OnPlayStop();

	KUJATA_API bool SetParent(GameObject* parent, bool keepWorldPosition = false);

	GameObject* GetParent() const { return parent_; }

	const std::vector<GameObject*>& GetChildren() const { return children_; }

	/// <summary>
	/// 直下の子childを、同じく直下の子referenceの直前/直後へ並び替える。
	/// childとreferenceは共に現在の直接の子である必要がある。
	/// </summary>
	KUJATA_API bool ReorderChild(GameObject* child, GameObject* reference, bool insertAfter);

	bool IsRoot() const { return parent_ == nullptr; }

	/// <summary>
	/// 階層を親へ辿った先の最上位GameObjectを返します(自身がRootならthis)。
	/// 「同じキャラクター/オブジェクトに属するか」の判定に使います
	/// (例: ガーディアンの脚ボーン同士は同じRootを持つので同一個体とみなせる)。
	/// </summary>
	KUJATA_API GameObject* GetHierarchyRoot();

	KUJATA_API bool IsDescendantOf(const GameObject* ancestor) const;

	KUJATA_API void UpdateHierarchy();

	KUJATA_API void DrawHierarchy();
	 
	/// <summary>
	/// 親から子へワールド行列を更新
	/// </summary>
	KUJATA_API void UpdateWorldTransformHierarchy();

	/// <summary>
	/// 親を先に更新して自身のワールド行列を更新
	/// </summary>
	KUJATA_API void UpdateWorldTransformSelfAndAncestors();

	/// <summary>
	/// 所属Sceneを取得します(Scene::AddGameObjectで設定される)。
	/// Componentから他のGameObjectを探す時に使います(UnityのgameObject.scene相当)。
	/// </summary>
	Scene* GetScene() const { return scene_; }

	void SetScene(Scene* scene) { scene_ = scene; }

	/// <summary>
	/// Scene保存/復元で使う安定IDを取得
	/// </summary>
	const std::string& GetInstanceId() const { return instanceId_; }

	/// <summary>
	/// Scene保存/復元で使う安定IDを設定
	/// </summary>
	KUJATA_API void SetInstanceId(const std::string& instanceId);

	KUJATA_API void SetPrefabLink(const std::string& assetPath, const std::string& objectId, const std::string& rootInstanceId, bool isRoot);

	KUJATA_API void ClearPrefabLink();

	bool IsPrefabInstance() const { return !prefabAssetPath_.empty() && !prefabObjectId_.empty(); }

	bool IsPrefabInstanceRoot() const { return IsPrefabInstance() && isPrefabInstanceRoot_; }

	const std::string& GetPrefabAssetPath() const { return prefabAssetPath_; }

	const std::string& GetPrefabObjectId() const { return prefabObjectId_; }

	const std::string& GetPrefabInstanceRootId() const { return prefabInstanceRootId_; }

	const std::string& GetName() const { return name_; }

	void SetName(const std::string& name) { name_ = name; }

	/// <summary>
	/// ゲームロジック分類用Tagを取得
	/// </summary>
	const std::string& GetTag() const { return tag_; }

	/// <summary>
	/// ゲームロジック分類用Tagを設定
	/// </summary>
	void SetTag(const std::string& tag) { tag_ = tag; }

	/// <summary>
	/// Tagが一致するかを返します(Unityの CompareTag 相当)。
	/// </summary>
	bool CompareTag(const std::string& tag) const { return tag_ == tag; }

	/// <summary>
	/// 衝突フィルタなどに使うLayerを取得
	/// </summary>
	uint32_t GetLayer() const { return layer_; }

	/// <summary>
	/// 衝突フィルタなどに使うLayerを設定
	/// </summary>
	KUJATA_API void SetLayer(uint32_t layer);

	bool IsActive() const { return active_; }

	/// <summary>
	/// 親階層を含めた有効状態を取得
	/// </summary>
	KUJATA_API bool IsActiveInHierarchy() const;

	void SetActive(bool active) { active_ = active; }

	KUJATA_API WorldTransform& GetTransform();

	KUJATA_API const WorldTransform& GetTransform() const;

	Component* GetTransformComponent() const { return transformComponent_; }

	KUJATA_API Component* EnsureTransformComponent();

	KUJATA_API Component* AddComponent(std::unique_ptr<Component> component);

	KUJATA_API void RemoveComponent(Component* component);

	KUJATA_API void RemoveComponentAt(size_t index);

	template <class T, class... Args>
	T* AddComponent(Args&&... args);

	template <class T>
	T* GetComponent();

	/// <summary>自身と子孫からT型Componentを探します(深さ優先、無ければnullptr)。</summary>
	template <class T>
	T* GetComponentInChildren();

	/// <summary>自身と祖先からT型Componentを探します(無ければnullptr)。</summary>
	template <class T>
	T* GetComponentInParent();

	std::vector<std::unique_ptr<Component>>& GetComponents() { return components_; }

	const std::vector<std::unique_ptr<Component>>& GetComponents() const { return components_; }

private:
	std::string instanceId_;
	Scene* scene_ = nullptr;
	std::string name_ = "GameObject";
	std::string tag_ = "Untagged";
	uint32_t layer_ = 0;
	bool active_ = true;
	bool initialized_ = false;
	bool transformInitialized_ = false;
	WorldTransform transform_;
	Component* transformComponent_ = nullptr;
	std::vector<std::unique_ptr<Component>> components_;
	GameObject* parent_ = nullptr;
	std::vector<GameObject*> children_;
	std::string prefabAssetPath_;
	std::string prefabObjectId_;
	std::string prefabInstanceRootId_;
	bool isPrefabInstanceRoot_ = false;
};

template <class T, class... Args>
T* GameObject::AddComponent(Args&&... args) {
	static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

	std::unique_ptr<T> component = std::make_unique<T>(std::forward<Args>(args)...);
	Component* raw = AddComponent(std::move(component));
	return static_cast<T*>(raw);
}

template <class T>
T* GameObject::GetComponent() {
	static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

	for (const std::unique_ptr<Component>& component : components_) {
		T* result = dynamic_cast<T*>(component.get());
		if (result) {
			return result;
		}
	}

	return nullptr;
}

template <class T>
T* GameObject::GetComponentInChildren() {
	if (T* result = GetComponent<T>()) {
		return result;
	}
	for (GameObject* child : children_) {
		if (!child) {
			continue;
		}
		if (T* result = child->GetComponentInChildren<T>()) {
			return result;
		}
	}
	return nullptr;
}

template <class T>
T* GameObject::GetComponentInParent() {
	for (GameObject* current = this; current; current = current->parent_) {
		if (T* result = current->GetComponent<T>()) {
			return result;
		}
	}
	return nullptr;
}

// --- Component側の検索(Unityと同じく、Component内から直接呼べる) ---

template <class T>
T* Component::GetComponent() const {
	return owner_ ? owner_->GetComponent<T>() : nullptr;
}

template <class T>
T* Component::GetComponentInChildren() const {
	return owner_ ? owner_->GetComponentInChildren<T>() : nullptr;
}

template <class T>
T* Component::GetComponentInParent() const {
	return owner_ ? owner_->GetComponentInParent<T>() : nullptr;
}

} // namespace KujataEngine
