#pragma once

#include "../runtime/KujataApi.h"
#include "ObjectRef.h"
#include <string>
#include <vector>

namespace KujataEngine {

class GameObject;

/// <summary>
/// UnityのUnityEventの1エントリ(persistent listener)相当。
/// 「どのGameObjectの・どのComponentの・どのメソッドを呼ぶか」を保持する。
/// target はinstanceIdでシリアライズされ、ロード後にGameObject*へ解決される。
/// </summary>
struct PersistentCall {
	ObjectRef target;          // 対象GameObject(instanceIdで永続化)
	std::string componentType; // 対象Componentの型名(Component::GetTypeName)
	std::string methodName;    // 呼び出すメソッド名(InvokableMethodRegistryに登録された名前)

	/// <summary>
	/// target上のcomponentType Componentを探し、methodNameのメソッドを呼ぶ。
	/// target未解決/対象不在/メソッド未登録なら何もしない。
	/// </summary>
	KUJATA_API void Invoke() const;
};

/// <summary>
/// UnityのUnityEvent(例: Button.onClick)相当。複数の永続呼び出しを順に発火する。
/// </summary>
struct UnityAction {
	std::vector<PersistentCall> calls;

	/// <summary>全ての呼び出しを順に発火する。</summary>
	KUJATA_API void Invoke() const;

	/// <summary>各呼び出しのtarget(instanceId)を実ポインタへ解決する(ロード後に呼ぶ)。</summary>
	void Resolve(const IObjectResolver& resolver) {
		for (PersistentCall& call : calls) {
			call.target.Resolve(resolver);
		}
	}
};

} // namespace KujataEngine
