#include "GameObject.h"
#include "ComponentFactory.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace KujataEngine {

namespace {

std::string GenerateInstanceId() {
	static std::atomic<uint64_t> counter = 0;
	static std::random_device randomDevice;
	static std::mt19937_64 randomEngine(randomDevice());

	const uint64_t now = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	const uint64_t sequence = ++counter;
	const uint64_t randomValue = randomEngine();

	std::ostringstream os;
	os << "go_";
	os << std::hex << std::setfill('0');
	os << std::setw(16) << (now ^ randomValue);
	os << std::setw(16) << (sequence ^ (randomValue >> 1));
	return os.str();
}

} // namespace

GameObject::GameObject(const std::string& name) : instanceId_(GenerateInstanceId()), name_(name) {
	EnsureTransformComponent();
}

GameObject::~GameObject() = default;

bool GameObject::SetParent(GameObject* parent, bool keepWorldPosition) {
	if (parent == this) {
		return false;
	}
	if (parent && parent->IsDescendantOf(this)) {
		return false;
	}

	Vector3 worldPosition = transform_.translation_;
	if (keepWorldPosition) {
		UpdateWorldTransformSelfAndAncestors();
		worldPosition = transform_.GetWorldPosition();
	}

	if (parent_ == parent) {
		if (parent) {
			transform_.parent_ = &parent->GetTransform();
		} else {
			transform_.parent_ = nullptr;
		}
		if (keepWorldPosition) {
			transform_.SetWorldPosition(worldPosition);
		}
		return true;
	}

	if (parent_) {
		std::vector<GameObject*>& siblings = parent_->children_;
		siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
	}

	parent_ = parent;
	if (parent_) {
		std::vector<GameObject*>& siblings = parent_->children_;
		if (std::find(siblings.begin(), siblings.end(), this) == siblings.end()) {
			siblings.push_back(this);
		}
		transform_.parent_ = &parent_->GetTransform();
	} else {
		transform_.parent_ = nullptr;
	}

	if (keepWorldPosition) {
		if (parent_) {
			parent_->UpdateWorldTransformSelfAndAncestors();
		}
		transform_.SetWorldPosition(worldPosition);
	}

	return true;
}

bool GameObject::ReorderChild(GameObject* child, GameObject* reference, bool insertAfter) {
	if (!child || child == reference) {
		return false;
	}

	std::vector<GameObject*>::iterator childIt = std::find(children_.begin(), children_.end(), child);
	if (childIt == children_.end()) {
		return false;
	}
	children_.erase(childIt);

	// referenceが子でない/未指定なら末尾へ。
	std::vector<GameObject*>::iterator referenceIt =
	    reference ? std::find(children_.begin(), children_.end(), reference) : children_.end();
	if (referenceIt == children_.end()) {
		children_.push_back(child);
		return true;
	}

	if (insertAfter) {
		++referenceIt;
	}
	children_.insert(referenceIt, child);
	return true;
}

GameObject* GameObject::GetHierarchyRoot() {
	GameObject* current = this;
	while (current->parent_) {
		current = current->parent_;
	}
	return current;
}

bool GameObject::IsDescendantOf(const GameObject* ancestor) const {
	if (!ancestor) {
		return false;
	}

	const GameObject* current = parent_;
	while (current) {
		if (current == ancestor) {
			return true;
		}
		current = current->parent_;
	}

	return false;
}

bool GameObject::IsActiveInHierarchy() const {
	if (!active_) {
		return false;
	}
	if (parent_) {
		return parent_->IsActiveInHierarchy();
	}

	return true;
}

void GameObject::SetInstanceId(const std::string& instanceId) {
	// 既存JSONにIDがない場合や壊れている場合は、生成済みIDを維持する。
	if (instanceId.empty()) {
		return;
	}

	instanceId_ = instanceId;
}

void GameObject::SetLayer(uint32_t layer) {
	if (layer > 31) {
		layer = 31;
	}

	layer_ = layer;
}

void GameObject::SetPrefabLink(const std::string& assetPath, const std::string& objectId, const std::string& rootInstanceId, bool isRoot) {
	prefabAssetPath_ = assetPath;
	prefabObjectId_ = objectId;
	prefabInstanceRootId_ = rootInstanceId;
	isPrefabInstanceRoot_ = isRoot;
}

void GameObject::ClearPrefabLink() {
	prefabAssetPath_.clear();
	prefabObjectId_.clear();
	prefabInstanceRootId_.clear();
	isPrefabInstanceRoot_ = false;
}

void GameObject::Initialize() {
	if (initialized_) {
		return;
	}

	EnsureTransformComponent();
	if (!transformInitialized_) {
		transform_.Initialize();
		transformInitialized_ = true;
	}

	for (const std::unique_ptr<Component>& component : components_) {
		if (component) {
			component->Initialize();
		}
	}

	initialized_ = true;
}

void GameObject::Update() {
	if (!active_) {
		return;
	}

	// **添字で回すこと。範囲forは使えない。**
	// Update中にAddComponentされるとcomponents_が再確保され、範囲forのイテレータが
	// 無効化されて以降のUpdateが解放済みメモリを触る(thisが壊れる)。
	for (size_t index = 0; index < components_.size(); ++index) {
		Component* component = components_[index].get();
		if (component && component->IsEnabled()) {
			component->Update();
		}
	}
}

void GameObject::UpdateHierarchy() {
	if (!active_) {
		return;
	}

	Update();
	// 自分のUpdateが子を増やす可能性があるため、ここも添字で回す(再確保対策)。
	for (size_t index = 0; index < children_.size(); ++index) {
		GameObject* child = children_[index];
		if (child) {
			child->UpdateHierarchy();
		}
	}
}

void GameObject::Draw() {
	if (!active_) {
		return;
	}

	for (const std::unique_ptr<Component>& component : components_) {
		if (component && component->IsEnabled()) {
			component->Draw();
		}
	}
}

void GameObject::DrawHierarchy() {
	if (!active_) {
		return;
	}

	Draw();
	for (GameObject* child : children_) {
		if (child) {
			child->DrawHierarchy();
		}
	}
}

void GameObject::UpdateWorldTransformHierarchy() {
	EnsureTransformComponent();
	transform_.UpdateWorldMatrix();

	for (GameObject* child : children_) {
		if (child) {
			child->UpdateWorldTransformHierarchy();
		}
	}
}

void GameObject::UpdateWorldTransformSelfAndAncestors() {
	if (parent_) {
		parent_->UpdateWorldTransformSelfAndAncestors();
	}

	EnsureTransformComponent();
	transform_.UpdateWorldMatrix();
}

void GameObject::Finalize() {
	std::vector<GameObject*> children = children_;
	for (GameObject* child : children) {
		if (child) {
			child->SetParent(nullptr);
		}
	}
	SetParent(nullptr);

	for (const std::unique_ptr<Component>& component : components_) {
		if (component) {
			component->SetOwner(nullptr);
		}
	}
	components_.clear();
	transformComponent_ = nullptr;
	initialized_ = false;
}

void GameObject::OnPlayStart() {
	for (const std::unique_ptr<Component>& component : components_) {
		if (component && component->IsEnabled()) {
			component->OnPlayStart();
		}
	}
}

void GameObject::OnPlayStop() {
	for (const std::unique_ptr<Component>& component : components_) {
		// OnPlayStopは後始末の通知なので、IsEnabledに関係なく全Componentへ届ける。
		// Play中に無効化されたComponent(再生中のAudioSource等)が後始末できなくなるため。
		if (component) {
			component->OnPlayStop();
		}
	}
}

WorldTransform& GameObject::GetTransform() {
	EnsureTransformComponent();
	return transform_;
}

const WorldTransform& GameObject::GetTransform() const {
	const_cast<GameObject*>(this)->EnsureTransformComponent();
	return transform_;
}

Component* GameObject::AddComponent(std::unique_ptr<Component> component) {
	if (!component) {
		return nullptr;
	}

	if (!component->AllowMultiple()) {
		const char* typeName = component->GetTypeName();
		for (const std::unique_ptr<Component>& current : components_) {
			if (current && std::string(current->GetTypeName()) == typeName) {
				return current.get();
			}
		}
	}

	Component* raw = component.get();
	raw->SetOwner(this);
	components_.push_back(std::move(component));

	if (raw->IsTransformComponent()) {
		transformComponent_ = raw;
	}

	if (initialized_) {
		raw->Initialize();
	}

	return raw;
}

void GameObject::RemoveComponent(Component* component) {
	if (!component) {
		return;
	}

	auto iterator = std::find_if(components_.begin(), components_.end(), [component](const std::unique_ptr<Component>& current) {
		return current.get() == component;
	});

	if (iterator == components_.end()) {
		return;
	}

	if (!(*iterator)->CanRemove()) {
		return;
	}

	(*iterator)->SetOwner(nullptr);
	components_.erase(iterator);
}

void GameObject::RemoveComponentAt(size_t index) {
	if (index >= components_.size()) {
		return;
	}

	if (!components_[index]->CanRemove()) {
		return;
	}

	components_[index]->SetOwner(nullptr);
	components_.erase(components_.begin() + index);
}

Component* GameObject::EnsureTransformComponent() {
	if (transformComponent_) {
		return transformComponent_;
	}

	for (const std::unique_ptr<Component>& component : components_) {
		if (!component) {
			continue;
		}
		if (component->IsTransformComponent()) {
			transformComponent_ = component.get();
			return transformComponent_;
		}
	}

	std::unique_ptr<Component> transformComponent = ComponentFactory::GetInstance().Create("Transform");
	if (!transformComponent) {
		transformComponent = ComponentFactory::GetInstance().Create("TransformComponent");
	}

	Component* added = AddComponent(std::move(transformComponent));
	if (added && added->IsTransformComponent()) {
		transformComponent_ = added;
	}

	return transformComponent_;
}

} // namespace KujataEngine
