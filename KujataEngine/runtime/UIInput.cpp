#include "UIInput.h"

namespace KujataEngine {
namespace {
UIPointerState gPointer;
UIInputMode gInputMode = UIInputMode::Pointer;
GameObject* gSelected = nullptr;
} // namespace

void SetUIPointer(const UIPointerState& state) { gPointer = state; }
const UIPointerState& GetUIPointer() { return gPointer; }

UIInputMode GetUIInputMode() { return gInputMode; }
void SetUIInputMode(UIInputMode mode) { gInputMode = mode; }

void SetUISelected(GameObject* selected) { gSelected = selected; }
GameObject* GetUISelected() { return gSelected; }

} // namespace KujataEngine
