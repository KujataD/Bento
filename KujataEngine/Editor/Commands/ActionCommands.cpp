// エディタの CUI のコマンド: 操作(アクション)。キーを押さずに、コマンドからゲームを動かして確かめる。
// 設計と一覧は .claude/editor-automation.md。アクションの仕組みは .claude/tick-replay.md §6。
#include "EditorCommandUtil.h"
#include "../../runtime/InputActionSystem.h"
#include "../InputActionEditing.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace KujataEngine {

namespace {

using namespace EditorCommandUtil;
using nlohmann::json;

// アクション1つ分の今の値と割り当て。
json DescribeAction(const InputActionDef& action, ActionState& state) {
	json entry;
	entry["name"] = action.name;
	entry["type"] = InputActionAsset::ToString(action.type);
	entry["bindings"] = action.bindings;
	entry["value"] = {state.Axis1D(action.name), state.Axis2D(action.name).y};
	entry["raw"] = {state.RawX(action.name), state.RawY(action.name)};
	entry["held"] = state.Held(action.name);
	return entry;
}

EditorCommandResult CommandActionList(const EditorCommandArgs&) {
	InputActionSystem* system = InputActionSystem::GetInstance();
	ActionState& state = system->GetActions();
	json actions = json::array();
	for (const InputActionDef& action : system->GetDefinitions()) {
		actions.push_back(DescribeAction(action, state));
	}
	json result;
	result["source"] = system->GetSourceDescription();
	result["deviceInput"] = system->IsDeviceInputEnabled();
	result["pendingCommands"] = system->GetQueue().GetPendingCount();
	result["actions"] = actions;
	return EditorCommandResult::Success(result);
}

EditorCommandResult CommandActionSet(const EditorCommandArgs& args) {
	if (args.Count() < 2) {
		return EditorCommandResult::Failure("使い方: action.set <アクション名> <値> [y の値](値は -1〜1。ボタンは 0 か 1)");
	}
	const std::string name = args.Get(0);
	InputActionSystem* system = InputActionSystem::GetInstance();

	const std::vector<InputActionDef>& definitions = system->GetDefinitions();
	const bool known = std::any_of(definitions.begin(), definitions.end(), [&name](const InputActionDef& action) { return action.name == name; });
	if (!known) {
		std::string names;
		for (const InputActionDef& action : definitions) {
			names += (names.empty() ? " " : ", ") + action.name;
		}
		return EditorCommandResult::Failure("そのアクションはありません: " + name + "。あるもの:" + names);
	}

	float x = 0.0f;
	float y = 0.0f;
	try {
		x = std::stof(args.Get(1));
		if (args.Count() > 2) {
			y = std::stof(args.Get(2));
		}
	} catch (const std::exception&) {
		return EditorCommandResult::Failure("値は数字で指定してください(-1〜1)。例: action.set Move 1 0");
	}

	// 出どころはエディタ(0)。Play 中なら次の更新の頭で反映される。
	system->PushAction(name, x, y, kCommandSourceEditor);
	json result;
	result["name"] = name;
	result["value"] = {x, y};
	result["hint"] = "次の更新で反映される(Play 中でなければ値は変わらない)";
	return EditorCommandResult::Success(result);
}

EditorCommandResult CommandActionReload(const EditorCommandArgs&) {
	std::string message;
	if (!InputActionSystem::GetInstance()->Reload(&message)) {
		return EditorCommandResult::Failure(message);
	}
	return CommandActionList(EditorCommandArgs("action.list"));
}

EditorCommandResult CommandActionDevice(const EditorCommandArgs& args) {
	InputActionSystem* system = InputActionSystem::GetInstance();
	bool enabled = !system->IsDeviceInputEnabled();
	if (args.Count() > 0 && !ParseBool(args.Get(0), enabled)) {
		return EditorCommandResult::Failure("on か off を指定してください(省略すると切り替え)。");
	}
	system->SetDeviceInputEnabled(enabled);
	json result;
	result["deviceInput"] = enabled;
	result["hint"] = enabled ? "キーボード・パッドの入力を読む" : "キーボード・パッドを読まない(action.set と AI だけで動かす)";
	return EditorCommandResult::Success(result);
}

// 編集の結果を返す。成功したら今の定義を付ける。
EditorCommandResult ToCommandResult(const InputActionEditing::Result& edit) {
	if (!edit.succeeded) {
		return EditorCommandResult::Failure(edit.message);
	}
	EditorCommandResult result = CommandActionList(EditorCommandArgs("action.list"));
	result.result["message"] = edit.message;
	return result;
}

bool ParseType(const std::string& text, InputActionType& outType) {
	std::string lower = text;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return InputActionAsset::TryParseType(lower, outType);
}

EditorCommandResult CommandActionAdd(const EditorCommandArgs& args) {
	if (args.Count() < 1) {
		return EditorCommandResult::Failure("使い方: action.add <名前> [button|axis1d|axis2d]");
	}
	InputActionType type = InputActionType::Button;
	if (args.Count() > 1 && !ParseType(args.Get(1), type)) {
		return EditorCommandResult::Failure("種類は button / axis1d / axis2d のどれかです: " + args.Get(1));
	}
	return ToCommandResult(InputActionEditing::AddAction(args.Get(0), type));
}

EditorCommandResult CommandActionRemove(const EditorCommandArgs& args) {
	if (args.Count() < 1) {
		return EditorCommandResult::Failure("使い方: action.remove <名前>");
	}
	return ToCommandResult(InputActionEditing::RemoveAction(args.Get(0)));
}

EditorCommandResult CommandActionRename(const EditorCommandArgs& args) {
	if (args.Count() < 2) {
		return EditorCommandResult::Failure("使い方: action.rename <名前> <新しい名前>");
	}
	return ToCommandResult(InputActionEditing::RenameAction(args.Get(0), args.Get(1)));
}

EditorCommandResult CommandActionType(const EditorCommandArgs& args) {
	InputActionType type = InputActionType::Button;
	if (args.Count() < 2 || !ParseType(args.Get(1), type)) {
		return EditorCommandResult::Failure("使い方: action.type <名前> <button|axis1d|axis2d>");
	}
	return ToCommandResult(InputActionEditing::SetType(args.Get(0), type));
}

EditorCommandResult CommandActionBind(const EditorCommandArgs& args) {
	if (args.Count() < 2) {
		return EditorCommandResult::Failure("使い方: action.bind <名前> <割り当て>(使える名前は action.bindings)");
	}
	return ToCommandResult(InputActionEditing::AddBinding(args.Get(0), args.Get(1)));
}

EditorCommandResult CommandActionUnbind(const EditorCommandArgs& args) {
	if (args.Count() < 2) {
		return EditorCommandResult::Failure("使い方: action.unbind <名前> <割り当て>");
	}
	return ToCommandResult(InputActionEditing::RemoveBinding(args.Get(0), args.Get(1)));
}

EditorCommandResult CommandActionBindings(const EditorCommandArgs&) {
	json result;
	for (InputActionType type : {InputActionType::Button, InputActionType::Axis1D, InputActionType::Axis2D}) {
		result[InputActionAsset::ToString(type)] = InputActionAsset::GetBindingNames(type);
	}
	result["hint"] = "axis1d には \"A:D\" のように「マイナス側:プラス側」でボタンを2つ書いてもよい";
	return EditorCommandResult::Success(result);
}

} // namespace

void RegisterActionCommands(EditorCommandRegistry& registry) {
	registry.Register("action.add", "action.add <名前> [button|axis1d|axis2d]", "アクションを足して保存する(種類の既定は button)", CommandActionAdd);
	registry.Register("action.remove", "action.remove <名前>", "アクションを消して保存する", CommandActionRemove);
	registry.Register("action.rename", "action.rename <名前> <新しい名前>", "アクションの名前を変えて保存する", CommandActionRename);
	registry.Register("action.type", "action.type <名前> <button|axis1d|axis2d>", "アクションの種類を変えて保存する(合わない割り当ては外れる)", CommandActionType);
	registry.Register("action.bind", "action.bind <名前> <割り当て>", "割り当てを足して保存する", CommandActionBind);
	registry.Register("action.unbind", "action.unbind <名前> <割り当て>", "割り当てを外して保存する", CommandActionUnbind);
	registry.Register("action.bindings", "action.bindings", "種類ごとに使える割り当ての名前", CommandActionBindings);
	registry.Register("action.list", "action.list", "アクションの一覧(割り当てと今の値)", CommandActionList);
	registry.Register("action.set", "action.set <アクション名> <値> [y の値]", "アクションの値をコマンドで流す(キーを押すのと同じ扱い)", CommandActionSet);
	registry.Register("action.reload", "action.reload", "Data/ProjectSettings/InputActions.json を読み直す", CommandActionReload);
	registry.Register("action.device", "action.device [on|off]", "キーボード・パッドからの入力を読むかどうか(省略すると切り替え)", CommandActionDevice);
}

} // namespace KujataEngine
