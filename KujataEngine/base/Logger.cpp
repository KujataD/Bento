#include "Logger.h"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace KujataEngine {

// DLLエクスポートクラスにSTL型メンバを持たせるとC4251が出るため、
// ログストリームはこの翻訳単位ローカルのstaticに保持する。
namespace {
std::ofstream logStream_;
}

void Logger::Initialize() {
#ifdef _DEBUG
	// ログのディレクトリを用意
	std::filesystem::create_directory("logs");
	// 現在時刻を取得(UTC時刻)
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	// ログファイルの名前にコンマ何秒はいらないので、削って秒にする
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	// 日本時間(PCの設定時間)に変換
	std::chrono::zoned_time localTime{std::chrono::current_zone(), nowSeconds};
	// formatを使って年月日_時分秒の文字列に変換
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);
	// 時刻を使ってファイル名を決定
	std::string logFilePath = std::string("logs/") + dateString + ".log";
	// ファイルを作って書き込み準備
	logStream_.open(logFilePath);
#endif
}

void Logger::Log(const std::string& message) {
#ifdef _DEBUG
	logStream_ << message << std::endl;
	OutputDebugStringA(message.c_str());
#else
	(void)message;
#endif
}

} // namespace KujataEngine
