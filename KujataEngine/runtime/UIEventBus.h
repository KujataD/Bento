#pragma once

#include "KujataApi.h"
#include <functional>
#include <string>

namespace KujataEngine {

/// <summary>
/// UIの名前付きイベントバス。ButtonのonClick等をエンジン/ゲーム境界をまたいで配信する。
/// ゲーム側はSubscribe(eventName, callback)で購読し、UI側はPublish(eventName)で発火する。
/// </summary>
class UIEventBus {
public:
	static KUJATA_API void Subscribe(const std::string& eventName, std::function<void()> callback);
	static KUJATA_API void Publish(const std::string& eventName);

	/// <summary>Scene切り替え等で購読を全消去する。</summary>
	static KUJATA_API void Clear();
};

} // namespace KujataEngine
