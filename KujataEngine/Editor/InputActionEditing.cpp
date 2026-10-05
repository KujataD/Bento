#include "InputActionEditing.h"

#include "../runtime/InputActionSystem.h"
#include <algorithm>
#include <utility>
#include <vector>

namespace KujataEngine {

namespace InputActionEditing {

namespace {

Result Fail(std::string message) { return Result{false, std::move(message)}; }

InputActionDef* Find(std::vector<InputActionDef>& actions, const std::string& name) {
	auto it = std::find_if(actions.begin(), actions.end(), [&name](const InputActionDef& action) { return action.name == name; });
	return it != actions.end() ? &*it : nullptr;
}

// 名前に空白を許すと、CUI(空白区切り)から指定できなくなる。
bool IsValidName(const std::string& name) {
	return !name.empty() && name.find_first_of(" \t\r\n") == std::string::npos;
}

// 編集した定義を保存して、エンジンが使う定義を置き換える。
Result Commit(const std::vector<InputActionDef>& actions, const std::string& message) {
	std::string saveMessage;
	if (!InputActionSystem::GetInstance()->SaveDefinitions(actions, &saveMessage)) {
		return Fail(saveMessage);
	}
	return Result{true, message};
}

// 定義を写して、指定のアクションを編集する。
template <typename Edit>
Result EditAction(const std::string& name, Edit edit) {
	std::vector<InputActionDef> actions = InputActionSystem::GetInstance()->GetDefinitions();
	InputActionDef* action = Find(actions, name);
	if (!action) {
		return Fail("そのアクションはありません: " + name);
	}
	std::string message;
	if (!edit(*action, actions, message)) {
		return Fail(message);
	}
	return Commit(actions, message);
}

} // namespace

Result AddAction(const std::string& name, InputActionType type) {
	if (!IsValidName(name)) {
		return Fail("名前が空か、空白を含んでいます: " + name);
	}
	std::vector<InputActionDef> actions = InputActionSystem::GetInstance()->GetDefinitions();
	if (Find(actions, name)) {
		return Fail("同じ名前のアクションがあります: " + name);
	}
	actions.push_back(InputActionDef{name, type, {}});
	return Commit(actions, "アクションを追加しました: " + name);
}

Result RemoveAction(const std::string& name) {
	std::vector<InputActionDef> actions = InputActionSystem::GetInstance()->GetDefinitions();
	const auto removed = std::erase_if(actions, [&name](const InputActionDef& action) { return action.name == name; });
	if (removed == 0) {
		return Fail("そのアクションはありません: " + name);
	}
	return Commit(actions, "アクションを削除しました: " + name);
}

Result RenameAction(const std::string& name, const std::string& newName) {
	return EditAction(name, [&](InputActionDef& action, std::vector<InputActionDef>& actions, std::string& message) {
		if (!IsValidName(newName)) {
			message = "名前が空か、空白を含んでいます: " + newName;
			return false;
		}
		if (newName != name && Find(actions, newName)) {
			message = "同じ名前のアクションがあります: " + newName;
			return false;
		}
		action.name = newName;
		message = "名前を変えました: " + name + " → " + newName;
		return true;
	});
}

Result SetType(const std::string& name, InputActionType type) {
	return EditAction(name, [&](InputActionDef& action, std::vector<InputActionDef>&, std::string& message) {
		action.type = type;
		std::erase_if(action.bindings, [type](const std::string& binding) { return !InputActionAsset::IsValidBinding(type, binding); });
		message = name + " の種類を " + InputActionAsset::ToString(type) + " にしました";
		return true;
	});
}

Result AddBinding(const std::string& name, const std::string& binding) {
	return EditAction(name, [&](InputActionDef& action, std::vector<InputActionDef>&, std::string& message) {
		if (!InputActionAsset::IsValidBinding(action.type, binding)) {
			message = std::string(InputActionAsset::ToString(action.type)) + " に使えない割り当てです: " + binding;
			return false;
		}
		if (std::find(action.bindings.begin(), action.bindings.end(), binding) != action.bindings.end()) {
			message = "もう割り当ててあります: " + binding;
			return false;
		}
		action.bindings.push_back(binding);
		message = name + " に " + binding + " を割り当てました";
		return true;
	});
}

Result SetBinding(const std::string& name, size_t index, const std::string& binding) {
	return EditAction(name, [&](InputActionDef& action, std::vector<InputActionDef>&, std::string& message) {
		if (index >= action.bindings.size()) {
			message = "割り当ての番号が範囲外です";
			return false;
		}
		if (!InputActionAsset::IsValidBinding(action.type, binding)) {
			message = std::string(InputActionAsset::ToString(action.type)) + " に使えない割り当てです: " + binding;
			return false;
		}
		action.bindings[index] = binding;
		message = name + " の割り当てを " + binding + " にしました";
		return true;
	});
}

Result RemoveBinding(const std::string& name, const std::string& binding) {
	return EditAction(name, [&](InputActionDef& action, std::vector<InputActionDef>&, std::string& message) {
		if (std::erase(action.bindings, binding) == 0) {
			message = name + " に " + binding + " は割り当てられていません";
			return false;
		}
		message = name + " から " + binding + " を外しました";
		return true;
	});
}

Result MoveAction(const std::string& name, int offset) {
	std::vector<InputActionDef> actions = InputActionSystem::GetInstance()->GetDefinitions();
	auto it = std::find_if(actions.begin(), actions.end(), [&name](const InputActionDef& action) { return action.name == name; });
	if (it == actions.end()) {
		return Fail("そのアクションはありません: " + name);
	}
	const ptrdiff_t from = it - actions.begin();
	const ptrdiff_t to = std::clamp<ptrdiff_t>(from + offset, 0, static_cast<ptrdiff_t>(actions.size()) - 1);
	if (from == to) {
		return Result{true, ""};
	}
	std::swap(actions[from], actions[to]);
	return Commit(actions, "並びを変えました: " + name);
}

std::string GetDefaultBinding(InputActionType type) {
	const std::vector<std::string> names = InputActionAsset::GetBindingNames(type);
	return names.empty() ? std::string() : names.front();
}

} // namespace InputActionEditing

} // namespace KujataEngine
