#include "EditorCommand.h"

#include <cctype>
#include <exception>
#include <utility>

namespace KujataEngine {

namespace {

bool IsSpace(char character) { return std::isspace(static_cast<unsigned char>(character)) != 0; }

} // namespace

EditorCommandArgs::EditorCommandArgs(const std::string& line) : line_(line) {
	// 空白で区切る。"..."の中は空白を含めて1つの引数にし、両端の"は外す。
	std::vector<std::string> tokens;
	std::vector<size_t> offsets;
	size_t index = 0;
	while (index < line_.size()) {
		while (index < line_.size() && IsSpace(line_[index])) {
			++index;
		}
		if (index >= line_.size()) {
			break;
		}

		offsets.push_back(index);
		std::string token;
		if (line_[index] == '"') {
			++index;
			while (index < line_.size() && line_[index] != '"') {
				token += line_[index];
				++index;
			}
			if (index < line_.size()) {
				++index; // 閉じの"
			}
		} else {
			while (index < line_.size() && !IsSpace(line_[index])) {
				token += line_[index];
				++index;
			}
		}
		tokens.push_back(std::move(token));
	}

	if (!tokens.empty()) {
		name_ = tokens.front();
		args_.assign(tokens.begin() + 1, tokens.end());
		argOffsets_.assign(offsets.begin() + 1, offsets.end());
	}
}

const std::string& EditorCommandArgs::Get(size_t index) const {
	static const std::string kEmpty;
	return index < args_.size() ? args_[index] : kEmpty;
}

std::string EditorCommandArgs::RestFrom(size_t index) const {
	if (index >= argOffsets_.size()) {
		return {};
	}
	std::string rest = line_.substr(argOffsets_[index]);
	while (!rest.empty() && IsSpace(rest.back())) {
		rest.pop_back();
	}
	return rest;
}

EditorCommandRegistry& EditorCommandRegistry::GetInstance() {
	static EditorCommandRegistry instance;
	return instance;
}

void EditorCommandRegistry::Register(const std::string& name, const std::string& usage, const std::string& description, Handler handler) {
	commands_[name] = Command{name, usage, description, std::move(handler)};
}

EditorCommandResult EditorCommandRegistry::Execute(const std::string& line) const {
	EditorCommandArgs args(line);
	if (args.GetName().empty()) {
		return EditorCommandResult::Failure("コマンドが空です。help で一覧を表示できます。");
	}

	auto found = commands_.find(args.GetName());
	if (found == commands_.end()) {
		return EditorCommandResult::Failure("不明なコマンドです: " + args.GetName() + "(help で一覧を表示できます)");
	}

	// コマンドの中で例外が出ても、エディタは落とさずに失敗として返す。
	try {
		return found->second.handler(args);
	} catch (const std::exception& exception) {
		return EditorCommandResult::Failure(std::string("コマンドの実行中に例外が発生しました: ") + exception.what());
	}
}

} // namespace KujataEngine
