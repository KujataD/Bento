#pragma once

#include "IAssetResolver.h"
#include "KujataApi.h"

namespace KujataEngine {

// 現在のアセットリゾルバを取得する。未設定時は素通しのフォールバックを返す。
KUJATA_API IAssetResolver& GetAssetResolver();

// アセットリゾルバを設定する(Editor側がAssetDatabaseを注入する)。
KUJATA_API void SetAssetResolver(IAssetResolver* resolver);

} // namespace KujataEngine
