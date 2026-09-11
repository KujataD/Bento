#include "SelectionProvider.h"

#include "EngineContext.h"

namespace KujataEngine {

namespace {

// プロバイダ未設定時のフォールバック。選択なし(nullptr)を返す。
class NullSelectionProvider : public ISelectionProvider {
public:
	GameObject* GetSelectedGameObject() const override { return nullptr; }
};

} // namespace

ISelectionProvider& GetSelectionProvider() {
	if (ISelectionProvider* provider = GetEngineContext().selectionProvider) {
		return *provider;
	}
	static NullSelectionProvider fallback;
	return fallback;
}

void SetSelectionProvider(ISelectionProvider* provider) { GetEngineContext().selectionProvider = provider; }

} // namespace KujataEngine
