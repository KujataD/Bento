#pragma once

#include "../runtime/KujataApi.h"
#include <functional>
#include <string>
#include <vector>

namespace KujataEngine {

/// <summary>
/// Componentが公開する「Inspectorから呼び出せるメソッド」を名前で集めるRegistry。
/// UnityのUnityEventがpersistent listenerで参照するメソッドに相当する。
/// C++にはリフレクションが無いため、各Componentが明示的にAdd(name, callable)で登録する
/// (BTアクション登録やSerializedFieldの登録と同じ発想)。
/// </summary>
class InvokableMethodRegistry {
public:
	struct Entry {
		std::string name;
		std::function<void()> callback;
	};

	/// <summary>引数なし(void())のメソッドを登録する。</summary>
	void Add(const std::string& name, std::function<void()> callback) {
		entries_.push_back({name, std::move(callback)});
	}

	const std::vector<Entry>& Entries() const { return entries_; }

	/// <summary>登録済みメソッドを名前で呼ぶ。見つかればtrue。</summary>
	bool Invoke(const std::string& name) const {
		for (const Entry& entry : entries_) {
			if (entry.name == name) {
				if (entry.callback) {
					entry.callback();
				}
				return true;
			}
		}
		return false;
	}

private:
	std::vector<Entry> entries_;
};

} // namespace KujataEngine
