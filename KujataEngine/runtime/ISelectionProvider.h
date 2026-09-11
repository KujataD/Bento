#pragma once

namespace KujataEngine {

class GameObject;

// Editorの選択状態を、ランタイム(デバッグ描画等)へ提供する契約。
// 実装(EditorSelection)はEditor側にあり、ランタイムはこの抽象越しに問い合わせる(依存性逆転)。
class ISelectionProvider {
public:
	virtual ~ISelectionProvider() = default;

	// 現在選択中のGameObject(なければnullptr)。
	virtual GameObject* GetSelectedGameObject() const = 0;
};

} // namespace KujataEngine
