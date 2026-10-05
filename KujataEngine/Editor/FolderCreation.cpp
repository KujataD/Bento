#include "FolderCreation.h"

#include "../base/ProjectPath.h"
#include "../base/StringUtil.h"
#include <algorithm>
#include <string_view>

namespace KujataEngine {

namespace FolderCreation {

namespace {

Result Fail(std::string message) {
	Result result;
	result.message = std::move(message);
	return result;
}

// Windows のファイル名に使えない文字・名前を弾く。
bool IsValidFolderName(const std::string& name) {
	if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ') {
		return false;
	}
	return std::none_of(name.begin(), name.end(), [](char c) {
		return static_cast<unsigned char>(c) < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos;
	});
}

} // namespace

Result CreateFolder(const std::filesystem::path& parent, const std::string& name) {
	if (!IsValidFolderName(name)) {
		return Fail("フォルダ名に使えない文字が含まれています: " + name);
	}

	const std::filesystem::path projectRoot = GetActiveProjectRoot().lexically_normal();
	const std::filesystem::path target = (std::filesystem::absolute(parent) / StringUtil::ToWString(name)).lexically_normal();
	const std::filesystem::path relative = target.lexically_relative(projectRoot);
	if (relative.empty() || *relative.begin() == "..") {
		return Fail("プロジェクトの外には作れません: " + StringUtil::ToString(target.wstring()));
	}

	std::error_code errorCode;
	if (std::filesystem::exists(target, errorCode)) {
		return Fail("同じ名前があります: " + name);
	}
	if (!std::filesystem::create_directories(target, errorCode)) {
		return Fail("フォルダを作れません: " + StringUtil::ToString(target.wstring()));
	}

	Result result;
	result.succeeded = true;
	result.path = target;
	result.message = "フォルダを作りました: " + StringUtil::ToString(relative.generic_wstring());
	return result;
}

} // namespace FolderCreation

} // namespace KujataEngine
