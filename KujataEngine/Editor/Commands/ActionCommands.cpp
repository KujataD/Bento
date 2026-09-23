// エディタの CUI のコマンド: 操作(アクション)。キーを押さずに、コマンドからゲームを動かして確かめる。
// 設計と一覧は .claude/editor-automation.md。アクションの仕組みは .claude/tick-replay.md §6。
#include "EditorCommandUtil.h"
#include "../../runtime/InputActionSystem.h"
#include <algorithm>
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

} // namespace

void RegisterActionCommands(EditorCommandRegistry& registry) {
	registry.Register("action.list", "action.list", "アクションの一覧(割り当てと今の値)", CommandActionList);
	registry.Register("action.set", "action.set <アクション名> <値> [y の値]", "アクションの値をコマンドで流す(キーを押すのと同じ扱い)", CommandActionSet);
	registry.Register("action.reload", "action.reload", "Data/ProjectSettings/InputActions.json を読み直す", CommandActionReload);
	registry.Register("action.device", "action.device [on|off]", "キーボード・パッドからの入力を読むかどうか(省略すると切り替え)", CommandActionDevice);
}

} // namespace KujataEngine
