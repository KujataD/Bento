#pragma once

#include "../../externals/nlohmann/json.hpp"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace KujataEngine {

/// <summary>
/// コマンド1行を空白で区切ったもの。"..."で囲んだ部分は1つの引数になる。
/// field.setの値のように「ここから行末まで」を受け取りたい場合はRestFromを使う。
/// </summary>
class EditorCommandArgs {
public:
	explicit EditorCommandArgs(const std::string& line);

	/// <summary>先頭の単語(コマンド名)。</summary>
	const std::string& GetName() const { return name_; }

	/// <summary>コマンド名を除いた引数の数。</summary>
	size_t Count() const { return args_.size(); }

	/// <summary>index番目の引数(無ければ空文字)。</summary>
	const std::string& Get(size_t index) const;

	/// <summary>index番目の引数から行末までを、元の文字列のまま返す(前後の空白は除く)。</summary>
	std::string RestFrom(size_t index) const;

private:
	std::string line_;
	std::string name_;
	std::vector<std::string> args_;
	// args_[i]が元の行のどこから始まるか。RestFromで使う。
	std::vector<size_t> argOffsets_;
};

/// <summary>
/// コマンド1回分の結果。すぐに返事ができないコマンドは次のどちらかを設定する。
///   - waitFrames: その分フレームが進むのを待ってから返事をする(waitコマンド用)
///   - poll      : 毎フレームの頭に呼ばれる。終わったら out に最終結果を書いて true を返す
///                 (view.screenshotのように、GPUの描画が終わるのを待つもの用)
/// </summary>
struct EditorCommandResult {
	bool ok = true;
	nlohmann::json result = nullptr;
	std::string error;
	int waitFrames = 0;
	std::function<bool(EditorCommandResult& out)> poll;

	static EditorCommandResult Success(nlohmann::json value = nullptr) {
		EditorCommandResult commandResult;
		commandResult.result = std::move(value);
		return commandResult;
	}

	static EditorCommandResult Failure(const std::string& message) {
		EditorCommandResult commandResult;
		commandResult.ok = false;
		commandResult.error = message;
		return commandResult;
	}
};

/// <summary>
/// エディタ操作のコマンドの登録簿。人間のConsole入力・kujata CLI・--runのスクリプトは、すべてここを通る。
/// 実行は必ずメインスレッドで行うこと(EditorCommandServerがフレームの頭で呼ぶ)。
/// </summary>
class EditorCommandRegistry {
public:
	using Handler = std::function<EditorCommandResult(const EditorCommandArgs& args)>;

	struct Command {
		std::string name;
		std::string usage;       // 例: "field.set <オブジェクト> <型名[#番号]> <キー> <値>"
		std::string description; // 人間が読む説明(1行)
		Handler handler;
	};

	static EditorCommandRegistry& GetInstance();

	void Register(const std::string& name, const std::string& usage, const std::string& description, Handler handler);

	/// <summary>1行を解析して実行する。未登録のコマンドは失敗を返す。</summary>
	EditorCommandResult Execute(const std::string& line) const;

	const std::map<std::string, Command>& GetCommands() const { return commands_; }

private:
	EditorCommandRegistry() = default;

	// help の一覧が名前順になるよう std::map で持つ。
	std::map<std::string, Command> commands_;
};

/// <summary>
/// 標準のコマンド(参照・選択・編集・実行)を登録する。EditorCommands.cppに実装がある。
/// </summary>
void RegisterBuiltinEditorCommands();

} // namespace KujataEngine
