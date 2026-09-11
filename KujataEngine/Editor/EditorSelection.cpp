#include "EditorSelection.h"

namespace KujataEngine {

EditorSelection* EditorSelection::GetInstance() {
	static EditorSelection instance;
	return &instance;
}

void EditorSelection::SetSelectedGameObject(GameObject* object) {
	selectedGameObject_ = object;
	selectedAssetPath_.clear();
	selectedAssetType_ = AssetType::Unknown;
}

void EditorSelection::SetSelectedAsset(const std::filesystem::path& assetPath, AssetType assetType) {
	selectedGameObject_ = nullptr;
	selectedAssetPath_ = assetPath;
	selectedAssetType_ = assetType;
}

GameObject* EditorSelection::GetSelectedGameObject() const {
	return selectedGameObject_;
}

const std::filesystem::path& EditorSelection::GetSelectedAssetPath() const {
	return selectedAssetPath_;
}

AssetType EditorSelection::GetSelectedAssetType() const {
	return selectedAssetType_;
}

void EditorSelection::Clear() {
	selectedGameObject_ = nullptr;
	selectedAssetPath_.clear();
	selectedAssetType_ = AssetType::Unknown;
}

} // namespace KujataEngine
