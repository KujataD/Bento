#pragma once

#include "ISelectionProvider.h"
#include "KujataApi.h"

namespace KujataEngine {

// 現在の選択プロバイダを取得する。未設定時は常にnullptrを返すフォールバックを返す。
KUJATA_API ISelectionProvider& GetSelectionProvider();

// 選択プロバイダを設定する(Editor側がEditorSelectionを注入する)。
KUJATA_API void SetSelectionProvider(ISelectionProvider* provider);

} // namespace KujataEngine
