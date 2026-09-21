#include "EditorLog.h"

#include "../../externals/nlohmann/json.hpp"
#include "../base/Logger.h"
#include "../base/ProjectPath.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <fstream>
#include <mutex>
#include <system_error>

namespace KujataEngine {

namespace {

std::ofstream& GetStream() {
	static std::ofstream stream;
	return stream;
}

std::filesystem::path& GetPath() {
	static std::filesystem::path path;
	return path;
}

std::string ToLower(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return text;
}

bool ContainsAny(const std::string& text, std::initializer_list<const char*> words) {
	for (const char* word : words) {
		if (text.find(word) != std::string::npos) {
			return true;
		}
	}
	return false;
}

std::string MakeTimestamp(const char* format) {
	// 秒で切り捨てる(ミリ秒まで持たせると、書式の %S が "02.025" のように小数を出してしまう)。
	const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
	const std::chrono::zoned_time localTime{std::chrono::current_zone(), now};
	return std::vformat(format, std::make_format_args(localTime));
}

void OnEngineLog(const std::string& message) {
	// Logger::Log は行末に改行を付けて渡されることがあるので外す。
	std::string trimmed = message;
	while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
		trimmed.pop_back();
	}
	if (!trimmed.empty()) {
		EditorLog::Write("Engine", trimmed);
	}
}

} // namespace

EditorLogLevel ClassifyEditorLog(const std::string& message) {
	const std::string lower = ToLower(message);
	if (ContainsAny(lower, {"error", "fail", "exception", "assert", "エラー", "失敗", "例外"})) {
		return EditorLogLevel::Error;
	}
	if (ContainsAny(lower, {"warn", "not found", "ignored", "unavailable", "警告", "見つかりません", "無視"})) {
		return EditorLogLevel::Warning;
	}
	return EditorLogLevel::Info;
}

std::string ExtractEditorLogCategory(const std::string& message) {
	if (message.size() < 3 || message.front() != '[') {
		return {};
	}
	const size_t close = message.find(']');
	if (close == std::string::npos || close > 32) {
		return {};
	}
	return message.substr(1, close - 1);
}

const char* ToEditorLogLevelName(EditorLogLevel level) {
	switch (level) {
	case EditorLogLevel::Error:
		return "error";
	case EditorLogLevel::Warning:
		return "warning";
	case EditorLogLevel::Info:
	default:
		return "info";
	}
}

bool ParseEditorLogLevel(const std::string& name, EditorLogLevel& level) {
	const std::string lower = ToLower(name);
	if (lower == "info") {
		level = EditorLogLevel::Info;
	} else if (lower == "warning" || lower == "warn") {
		level = EditorLogLevel::Warning;
	} else if (lower == "error") {
		level = EditorLogLevel::Error;
	} else {
		return false;
	}
	return true;
}

void EditorLog::Write(const std::string& source, const std::string& message, EditorLogLevel level) {
	// Logger::Log は別スレッド(シェーダーのコンパイル等)から呼ばれることもあるので、書き込みは1本ずつにする。
	static std::mutex mutex;
	std::lock_guard<std::mutex> lock(mutex);
	std::ofstream& stream = GetStream();
	// 最初の1件で開く(エンジンのフォルダは起動時に決まっているので、いつ呼ばれても同じ場所)。
	if (!stream.is_open() && GetPath().empty()) {
		std::filesystem::path directory = GetEngineRoot() / "logs";
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		GetPath() = directory / ("editor_" + MakeTimestamp("{:%Y%m%d_%H%M%S}") + ".jsonl");
		stream.open(GetPath(), std::ios::trunc);
	}
	if (!stream.is_open()) {
		return;
	}

	nlohmann::json line;
	line["time"] = MakeTimestamp("{:%H:%M:%S}");
	line["level"] = ToEditorLogLevelName(level);
	line["source"] = source;
	line["category"] = ExtractEditorLogCategory(message);
	// ログに壊れたUTF-8が混ざっていても書き出しで例外にしない(置き換えて残す)。
	line["message"] = message;
	stream << line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
	stream.flush();
}

std::filesystem::path EditorLog::GetFilePath() { return GetPath(); }

void EditorLog::HookEngineLogger() { Logger::SetListener(OnEngineLog); }

} // namespace KujataEngine
