#pragma once

#include "AssetDatabase.h"
#include "../runtime/ISelectionProvider.h"
#include <filesystem>

namespace KujataEngine {

class GameObject;

/// <summary>
/// Editor内の選択状態を管理する
/// </summary>
class EditorSelection : public ISelectionProvider {
public:
	static EditorSelection* GetInstance();

	void SetSelectedGameObject(GameObject* object);

	void SetSelectedAsset(const std::filesystem::path& assetPath, AssetType assetType);

	GameObject* GetSelectedGameObject() const override;

	const std::filesystem::path& GetSelectedAssetPath() const;

	AssetType GetSelectedAssetType() const;

	void Clear();

private:
	EditorSelection() = default;
	~EditorSelection() = default;
	EditorSelection(const EditorSelection&) = delete;
	EditorSelection& operator=(const EditorSelection&) = delete;

private:
	// 所有権はSceneが持つ。Scene破棄時はEditorApplication側でClearする。
	GameObject* selectedGameObject_ = nullptr;
	// Project Window上で選んだAsset。Asset自体の所有権はファイルシステムが持つ。
	std::filesystem::path selectedAssetPath_;
	AssetType selectedAssetType_ = AssetType::Unknown;
};

} // namespace KujataEngine
