#include "StringUtil.h"


namespace KujataEngine {

// UTF-8として解釈してUTF-16へ変換する。日本語を含むパスでも壊れないよう、
// std::wstring(str.begin(), str.end())のようなバイト単位のコピーは使わない。
std::wstring StringUtil::ToWString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	int length = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
	if (length <= 0) {
		return std::wstring();
	}

	std::wstring result(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), length);
	return result;
}

// UTF-16をUTF-8へ変換する(ログ出力やUTF-8前提の外部ライブラリへの受け渡しに使う)。
std::string StringUtil::ToString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	int length = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0, nullptr, nullptr);
	if (length <= 0) {
		return std::string();
	}

	std::string result(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), length, nullptr, nullptr);
	return result;
}

} // namespace KujataEngine
