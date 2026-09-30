#pragma once

#include <filesystem>
#include <string>

namespace KujataEngine {

/// <summary>
/// ゲームのコンポーネントのひな形(.h / .cpp)を作る。
/// Project ウィンドウの Create > Script と CUI の script.create は、どちらもここを呼ぶ。
/// </summary>
namespace ScriptCreation {

struct Result {
	bool succeeded = false;
	std::string className;
	std::filesystem::path headerPath;
	std::filesystem::path sourcePath;
	std::string message;
};

/// <summary>
/// directory に <名前>.h / .cpp を作る。名前が Component で終わっていなければ付け足す。
/// プロジェクトの外や、ビルドに入らないフォルダ(Data・Temp・GameModule/bin)には作らない。
/// </summary>
Result CreateComponentScript(const std::filesystem::path& directory, const std::string& name);

} // namespace ScriptCreation

} // namespace KujataEngine
