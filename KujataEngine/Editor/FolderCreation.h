#pragma once

#include <filesystem>
#include <string>

namespace KujataEngine {

/// <summary>
/// プロジェクトの中にフォルダを作る。Project ウィンドウの Create > Folder と CUI の folder.create は、どちらもここを呼ぶ。
/// </summary>
namespace FolderCreation {

struct Result {
	bool succeeded = false;
	std::filesystem::path path;
	std::string message;
};

/// <summary>parent の中に name のフォルダを作る。プロジェクトの外・名前に使えない文字・同じ名前があるときは失敗。</summary>
Result CreateFolder(const std::filesystem::path& parent, const std::string& name);

} // namespace FolderCreation

} // namespace KujataEngine
