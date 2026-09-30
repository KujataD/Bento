#pragma once

#include "ComponentFactory.h"
#include <string>
#include <vector>

namespace KujataEngine {

/// <summary>
/// ゲーム DLL のコンポーネントを、GameModule.cpp に書き足さずに登録する仕組み。
/// コンポーネントの .cpp の末尾に `KUJATA_REGISTER_GAME_COMPONENT(型名);` を書くと DLL の読み込み時にここへ積まれ、
/// GameModule.cpp の RegisterGameComponents が RegisterAll でまとめて ComponentFactory へ登録する。
/// ヘッダだけで完結させ、一覧は DLL ごとに持つ(inline 関数の static。エンジンの exe とは共有しない)。
/// </summary>
namespace GameComponentRegistry {

using RegisterFunction = void (*)(ComponentFactory& factory, const std::string& moduleName);

inline std::vector<RegisterFunction>& GetList() {
	static std::vector<RegisterFunction> list;
	return list;
}

inline bool Add(RegisterFunction function) {
	GetList().push_back(function);
	return true;
}

inline void RegisterAll(ComponentFactory& factory, const std::string& moduleName) {
	for (RegisterFunction function : GetList()) {
		function(factory, moduleName);
	}
}

} // namespace GameComponentRegistry

} // namespace KujataEngine

#define KUJATA_GAME_COMPONENT_CONCAT_INNER(a, b) a##b
#define KUJATA_GAME_COMPONENT_CONCAT(a, b) KUJATA_GAME_COMPONENT_CONCAT_INNER(a, b)

/// <summary>コンポーネントをゲーム DLL に登録する。.cpp の末尾(名前空間の外)に 1 行書く。</summary>
#define KUJATA_REGISTER_GAME_COMPONENT(Type)                                                                                          \
	namespace {                                                                                                                       \
	const bool KUJATA_GAME_COMPONENT_CONCAT(kujataGameComponentRegistered_, __COUNTER__) = ::KujataEngine::GameComponentRegistry::Add( \
	    [](::KujataEngine::ComponentFactory& factory, const std::string& moduleName) { factory.RegisterComponent<Type>(moduleName); }); \
	}
