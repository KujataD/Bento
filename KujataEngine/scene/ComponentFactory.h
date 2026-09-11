#pragma once

#include "../runtime/KujataApi.h"
#include "Component.h"
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace KujataEngine {

/// <summary>
/// 登録済みComponentを型名から生成するFactory
/// </summary>
class ComponentFactory {
public:
	using CreateFunc = std::function<std::unique_ptr<Component>()>;

	static KUJATA_API ComponentFactory& GetInstance();

	KUJATA_API void Register(const std::string& typeName, CreateFunc createFunc);

	KUJATA_API void Register(const std::string& typeName, const std::string& moduleName, CreateFunc createFunc);

	/// <summary>
	/// ComponentのGetTypeNameを登録名として生成関数を登録
	/// </summary>
	template <class T>
	void RegisterComponent();

	/// <summary>
	/// ComponentのGetTypeNameを登録名として指定Moduleの生成関数を登録
	/// </summary>
	template <class T>
	void RegisterComponent(const std::string& moduleName);

	KUJATA_API void Unregister(const std::string& typeName);

	/// <summary>
	/// 指定Module由来のComponent登録を解除
	/// </summary>
	KUJATA_API void UnregisterByModule(const std::string& moduleName);

	KUJATA_API std::unique_ptr<Component> Create(const std::string& typeName) const;

	KUJATA_API const std::vector<std::string>& GetRegisteredTypeNames() const;
private:
	ComponentFactory() = default;
	~ComponentFactory() = default;
	ComponentFactory(const ComponentFactory&) = delete;
	ComponentFactory& operator=(const ComponentFactory&) = delete;

private:
	struct Entry {
		CreateFunc createFunc;
		std::string moduleName;
	};

	std::unordered_map<std::string, Entry> entries_;
	std::vector<std::string> typeNames_;
};

template <class T>
void ComponentFactory::RegisterComponent() {
	RegisterComponent<T>("Engine");
}

template <class T>
void ComponentFactory::RegisterComponent(const std::string& moduleName) {
	static_assert(std::is_base_of_v<Component, T>, "T must inherit KujataEngine::Component.");
	static_assert(std::is_default_constructible_v<T>, "T must be default constructible.");

	T component;
	Register(component.GetTypeName(), moduleName, []() {
		return std::make_unique<T>();
	});
}

} // namespace KujataEngine
