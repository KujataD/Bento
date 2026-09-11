#pragma once

#include "../runtime/KujataApi.h"
#include <string>

namespace KujataEngine {

class GameObject;

/// <summary>
/// instanceId から GameObject* を解決する抽象(port)。
/// 参照の"解決"はSceneに属する関心事だが、ドメイン側(ObjectRef/Component)は
/// この抽象にのみ依存し、Sceneの具象には依存しない(依存方向を内側へ保つ)。
/// </summary>
class IObjectResolver {
public:
	virtual ~IObjectResolver() = default;

	/// <summary>
	/// instanceId に対応するGameObjectを返す。該当が無ければnullptr。
	/// </summary>
	virtual GameObject* ResolveInstanceId(const std::string& instanceId) const = 0;
};

// --- GameObjectの完全定義をこのヘッダへ持ち込まないための関数境界 ---
// ObjectRef/ComponentRefのinlineコードはGameObjectをポインタ渡しするだけで済むよう、
// 完全定義が要る処理はKujataEngine.dll側(ObjectRef.cpp)の関数へ委譲する。
KUJATA_API std::string GameObjectDisplayName(const GameObject* gameObject);
KUJATA_API std::string GameObjectInstanceId(const GameObject* gameObject);
KUJATA_API GameObject* ResolveGameObject(const IObjectResolver& resolver, const std::string& instanceId);

namespace detail {
// GameObjectがこのヘッダでは前方宣言のみ(不完全型)なので、GetComponent<T>を直接呼ぶと
// テンプレート第1段階の名前解決で失敗する。オブジェクト型を型パラメータにして式を
// 型依存にし、解決を実体化時(GameObjectが完全なTU)へ遅延させる。
template <class T, class GameObjectType>
T* GetComponentOf(GameObjectType* gameObject) {
	return gameObject ? gameObject->template GetComponent<T>() : nullptr;
}
} // namespace detail

/// <summary>
/// UnityのInspectorでGameObjectを代入する参照フィールド(型なし)。
/// value が実行時の解決済み参照、targetInstanceId が保存/復元の軸。
/// </summary>
struct ObjectRef {
	GameObject* value = nullptr;  // 解決済みの参照(実行時のみ有効)
	std::string targetInstanceId; // 保存/復元の軸(これだけがシリアライズされる)

	GameObject* Get() const { return value; }
	bool IsSet() const { return value != nullptr; }
	explicit operator bool() const { return value != nullptr; }

	/// <summary>ドロップされたGameObjectを代入する。</summary>
	void Assign(GameObject* gameObject) {
		value = gameObject;
		targetInstanceId = GameObjectInstanceId(gameObject);
	}

	void Clear() {
		value = nullptr;
		targetInstanceId.clear();
	}

	/// <summary>保存済みのtargetInstanceIdからvalueを解決する(ロード後に呼ぶ)。</summary>
	void Resolve(const IObjectResolver& resolver) {
		value = ResolveGameObject(resolver, targetInstanceId);
	}
};

/// <summary>
/// 型付きの参照(Unityの public T target; 相当)。
/// ドロップされたGameObjectから自動でGetComponent&lt;T&gt;して T* を保持する。
/// 型が一致しないGameObjectのドロップは無視される。
/// </summary>
template <class T>
struct ComponentRef {
	GameObject* owner = nullptr;  // 参照先のGameObject(表示/再解決用)
	T* component = nullptr;       // 解決済みのComponent(実行時のみ有効)
	std::string targetInstanceId; // 保存/復元の軸

	T* Get() const { return component; }
	T* operator->() const { return component; }
	T& operator*() const { return *component; }
	bool IsSet() const { return component != nullptr; }
	explicit operator bool() const { return component != nullptr; }

	// GetComponent<T> に GameObject 完全定義が要るため、テンプレートとして
	// 利用側(GameObjectをincude済みのTU)で実体化させる。定義はヘッダ末尾。
	void Assign(GameObject* gameObject);
	void Resolve(const IObjectResolver& resolver);

	void Clear() {
		owner = nullptr;
		component = nullptr;
		targetInstanceId.clear();
	}
};

template <class T>
void ComponentRef<T>::Assign(GameObject* gameObject) {
	T* found = detail::GetComponentOf<T>(gameObject);
	if (!found) {
		// 型不一致のドロップは無視する(Unityの型付きフィールドと同じ挙動)。
		return;
	}
	owner = gameObject;
	component = found;
	targetInstanceId = GameObjectInstanceId(gameObject);
}

template <class T>
void ComponentRef<T>::Resolve(const IObjectResolver& resolver) {
	GameObject* gameObject = ResolveGameObject(resolver, targetInstanceId);
	owner = gameObject;
	component = detail::GetComponentOf<T>(gameObject);
}

} // namespace KujataEngine
