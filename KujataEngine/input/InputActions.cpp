#include "InputActions.h"

#include "../base/ProjectPath.h"
#include "Input.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_map>

namespace KujataEngine {

namespace {

using json = nlohmann::json;

// 割り当てに書けるキーの名前(左)と DirectInput のキーコード(右)。
// 増やすときはここだけに足す(ゲームのコードにキーコードを出さないため)。
const std::unordered_map<std::string, unsigned char>& GetKeyTable() {
	static const std::unordered_map<std::string, unsigned char> table = {
	    {"A", DIK_A}, {"B", DIK_B}, {"C", DIK_C}, {"D", DIK_D}, {"E", DIK_E}, {"F", DIK_F}, {"G", DIK_G}, {"H", DIK_H}, {"I", DIK_I},
	    {"J", DIK_J}, {"K", DIK_K}, {"L", DIK_L}, {"M", DIK_M}, {"N", DIK_N}, {"O", DIK_O}, {"P", DIK_P}, {"Q", DIK_Q}, {"R", DIK_R},
	    {"S", DIK_S}, {"T", DIK_T}, {"U", DIK_U}, {"V", DIK_V}, {"W", DIK_W}, {"X", DIK_X}, {"Y", DIK_Y}, {"Z", DIK_Z},
	    {"0", DIK_0}, {"1", DIK_1}, {"2", DIK_2}, {"3", DIK_3}, {"4", DIK_4}, {"5", DIK_5}, {"6", DIK_6}, {"7", DIK_7}, {"8", DIK_8},
	    {"9", DIK_9}, {"Space", DIK_SPACE}, {"Enter", DIK_RETURN}, {"Escape", DIK_ESCAPE}, {"Tab", DIK_TAB}, {"Shift", DIK_LSHIFT},
	    {"RightShift", DIK_RSHIFT}, {"Ctrl", DIK_LCONTROL}, {"RightCtrl", DIK_RCONTROL}, {"Alt", DIK_LMENU}, {"Backspace", DIK_BACK},
	    {"Up", DIK_UP}, {"Down", DIK_DOWN}, {"Left", DIK_LEFT}, {"Right", DIK_RIGHT},
	    {"F1", DIK_F1}, {"F2", DIK_F2}, {"F3", DIK_F3}, {"F4", DIK_F4}, {"F5", DIK_F5}, {"F6", DIK_F6},
	};
	return table;
}

// 割り当てに書けるパッドのボタンの名前と XInput のビット(L2 / R2 はトリガーなので別扱い。ReadButton)。
const std::unordered_map<std::string, WORD>& GetPadTable() {
	static const std::unordered_map<std::string, WORD> table = {
	    {"PadA", XINPUT_GAMEPAD_A},
	    {"PadB", XINPUT_GAMEPAD_B},
	    {"PadX", XINPUT_GAMEPAD_X},
	    {"PadY", XINPUT_GAMEPAD_Y},
	    {"PadL1", XINPUT_GAMEPAD_LEFT_SHOULDER},
	    {"PadR1", XINPUT_GAMEPAD_RIGHT_SHOULDER},
	    {"PadL3", XINPUT_GAMEPAD_LEFT_THUMB},
	    {"PadR3", XINPUT_GAMEPAD_RIGHT_THUMB},
	    {"PadStart", XINPUT_GAMEPAD_START},
	    {"PadBack", XINPUT_GAMEPAD_BACK},
	    {"PadUp", XINPUT_GAMEPAD_DPAD_UP},
	    {"PadDown", XINPUT_GAMEPAD_DPAD_DOWN},
	    {"PadLeft", XINPUT_GAMEPAD_DPAD_LEFT},
	    {"PadRight", XINPUT_GAMEPAD_DPAD_RIGHT},
	};
	return table;
}

// エディタの選択肢に出すパッドのボタンの並び(表は unordered_map なので並びを別に持つ)。
constexpr const char* kPadButtonOrder[] = {
    "PadA", "PadB", "PadX", "PadY", "PadL1", "PadR1", "PadL2", "PadR2", "PadL3", "PadR3",
    "PadStart", "PadBack", "PadUp", "PadDown", "PadLeft", "PadRight",
};

// L2 / R2 をボタンとして読むときの、押したとみなす引き具合(XInput の推奨値 30/255)。
constexpr float kTriggerButtonThreshold = XINPUT_GAMEPAD_TRIGGER_THRESHOLD / 255.0f;

int32_t Quantize(float value) {
	const float clamped = std::clamp(value, -1.0f, 1.0f);
	return static_cast<int32_t>(std::lround(clamped * static_cast<float>(kActionAxisMax)));
}

// キー・マウス・パッドのボタン1つ。押していれば kActionAxisMax。
bool ReadButton(const std::string& name, int32_t& outValue) {
	if (const auto key = GetKeyTable().find(name); key != GetKeyTable().end()) {
		outValue = Input::GetKey(key->second) ? kActionAxisMax : 0;
		return true;
	}
	if (const auto pad = GetPadTable().find(name); pad != GetPadTable().end()) {
		outValue = Input::GetControllerButton(pad->second) ? kActionAxisMax : 0;
		return true;
	}
	if (name == "PadL2" || name == "PadR2") {
		const float trigger = (name == "PadL2") ? Input::GetLeftTrigger() : Input::GetRightTrigger();
		outValue = (trigger > kTriggerButtonThreshold) ? kActionAxisMax : 0;
		return true;
	}
	if (name == "MouseLeft" || name == "MouseRight" || name == "MouseMiddle") {
		const int button = (name == "MouseLeft") ? 0 : (name == "MouseRight") ? 1 : 2;
		outValue = Input::GetClick(button) ? kActionAxisMax : 0;
		return true;
	}
	return false;
}

// 上下左右のキーから軸2Dを作る。
void ReadKeyPad(unsigned char up, unsigned char down, unsigned char left, unsigned char right, int32_t& outX, int32_t& outY) {
	const int32_t x = (Input::GetKey(right) ? 1 : 0) - (Input::GetKey(left) ? 1 : 0);
	const int32_t y = (Input::GetKey(up) ? 1 : 0) - (Input::GetKey(down) ? 1 : 0);
	outX = x * kActionAxisMax;
	outY = y * kActionAxisMax;
}

} // namespace

std::filesystem::path InputActionAsset::GetDefaultPath() { return GetProjectDataRoot() / "ProjectSettings" / "InputActions.json"; }

const char* InputActionAsset::ToString(InputActionType type) {
	switch (type) {
	case InputActionType::Axis1D:
		return "axis1d";
	case InputActionType::Axis2D:
		return "axis2d";
	case InputActionType::Button:
	default:
		return "button";
	}
}

bool InputActionAsset::TryParseType(const std::string& text, InputActionType& outType) {
	if (text == "button") {
		outType = InputActionType::Button;
		return true;
	}
	if (text == "axis1d") {
		outType = InputActionType::Axis1D;
		return true;
	}
	if (text == "axis2d") {
		outType = InputActionType::Axis2D;
		return true;
	}
	return false;
}

std::vector<InputActionDef> InputActionAsset::CreateDefault() {
	return {
	    {"Move", InputActionType::Axis2D, {"WASD", "LeftStick"}},
	    {"Look", InputActionType::Axis2D, {"Arrows", "RightStick"}},
	    {"Jump", InputActionType::Button, {"Space", "PadA"}},
	    {"Attack", InputActionType::Button, {"MouseLeft", "PadX"}},
	    {"Dash", InputActionType::Button, {"Shift", "PadB"}},
	};
}

std::vector<std::string> InputActionAsset::GetBindingNames(InputActionType type) {
	std::vector<std::string> names;
	switch (type) {
	case InputActionType::Axis2D:
		names = {"WASD", "Arrows", "LeftStick", "RightStick"};
		break;
	case InputActionType::Axis1D:
		names = {"LeftTrigger", "RightTrigger"};
		break;
	case InputActionType::Button:
	default: {
		// 表は unordered_map なので、選択肢の並びを決めるために並べ替える。
		std::vector<std::string> keys;
		for (const auto& [name, code] : GetKeyTable()) {
			keys.push_back(name);
		}
		std::sort(keys.begin(), keys.end(), [](const std::string& a, const std::string& b) {
			// 1文字のキー(A〜Z・0〜9)を先に、長い名前を後に並べる。
			if ((a.size() == 1) != (b.size() == 1)) {
				return a.size() == 1;
			}
			return a < b;
		});
		names = std::move(keys);
		names.insert(names.end(), {"MouseLeft", "MouseRight", "MouseMiddle"});
		names.insert(names.end(), std::begin(kPadButtonOrder), std::end(kPadButtonOrder));
		break;
	}
	}
	return names;
}

bool InputActionAsset::IsButtonName(const std::string& name) {
	return GetKeyTable().contains(name) || GetPadTable().contains(name) || name == "PadL2" || name == "PadR2" || name == "MouseLeft" ||
	       name == "MouseRight" || name == "MouseMiddle";
}

std::string InputActionAsset::GetBindingLabel(const std::string& binding) {
	// パッドは機種で呼び名が違うので、Xbox の名前を並べて出す。
	static const std::unordered_map<std::string, const char*> suffixes = {
	    {"PadL1", "LB"}, {"PadR1", "RB"}, {"PadL2", "LT"}, {"PadR2", "RT"}, {"PadL3", "LS Click"}, {"PadR3", "RS Click"},
	    {"PadStart", "Menu"}, {"PadBack", "View"}, {"LeftTrigger", "LT"}, {"RightTrigger", "RT"},
	};
	if (const auto it = suffixes.find(binding); it != suffixes.end()) {
		return binding + " (" + it->second + ")";
	}
	return binding;
}

bool InputActionAsset::IsValidBinding(InputActionType type, const std::string& binding) {
	if (type == InputActionType::Button) {
		return IsButtonName(binding);
	}
	if (type == InputActionType::Axis1D) {
		if (const size_t separator = binding.find(':'); separator != std::string::npos) {
			return IsButtonName(binding.substr(0, separator)) && IsButtonName(binding.substr(separator + 1));
		}
	}
	const std::vector<std::string> names = GetBindingNames(type);
	return std::find(names.begin(), names.end(), binding) != names.end();
}

bool InputActionAsset::Save(const std::filesystem::path& path, const std::vector<InputActionDef>& actions, std::string& outMessage) {
	// 人が読む・差分を見るファイルなので、キーを name → type → bindings の順に書く。
	nlohmann::ordered_json root;
	root["actions"] = nlohmann::ordered_json::array();
	for (const InputActionDef& action : actions) {
		nlohmann::ordered_json entry;
		entry["name"] = action.name;
		entry["type"] = ToString(action.type);
		entry["bindings"] = action.bindings;
		root["actions"].push_back(std::move(entry));
	}

	std::error_code errorCode;
	std::filesystem::create_directories(path.parent_path(), errorCode);
	std::ofstream file(path, std::ios::trunc);
	if (!file.is_open()) {
		outMessage = "InputActions.json に書き込めません: " + path.string();
		return false;
	}
	file << root.dump(2) << '\n';
	outMessage = "保存しました: " + path.string();
	return true;
}

bool InputActionAsset::Load(const std::filesystem::path& path, std::vector<InputActionDef>& outActions, std::string& outMessage) {
	std::error_code errorCode;
	if (path.empty() || !std::filesystem::exists(path, errorCode)) {
		outActions = CreateDefault();
		outMessage = "InputActions.json が無いので、既定のアクションを使います: " + path.string();
		return true;
	}

	std::ifstream file(path);
	if (!file.is_open()) {
		outMessage = "InputActions.json を開けません: " + path.string();
		return false;
	}

	json root;
	try {
		file >> root;
	} catch (const json::exception& exception) {
		outMessage = std::string("InputActions.json を読めません: ") + exception.what();
		return false;
	}

	if (!root.contains("actions") || !root.at("actions").is_array()) {
		outMessage = "InputActions.json に actions の配列がありません。";
		return false;
	}

	outActions.clear();
	for (const json& actionJson : root.at("actions")) {
		if (!actionJson.is_object() || !actionJson.contains("name") || !actionJson.at("name").is_string()) {
			continue;
		}
		InputActionDef action;
		action.name = actionJson.at("name").get<std::string>();
		if (actionJson.contains("type") && actionJson.at("type").is_string()) {
			InputActionAsset::TryParseType(actionJson.at("type").get<std::string>(), action.type);
		}
		if (actionJson.contains("bindings") && actionJson.at("bindings").is_array()) {
			for (const json& binding : actionJson.at("bindings")) {
				if (binding.is_string()) {
					action.bindings.push_back(binding.get<std::string>());
				}
			}
		}
		outActions.push_back(std::move(action));
	}
	outMessage = "読み込みました: " + path.string();
	return true;
}

void InputActionAsset::ReadBinding(const std::string& binding, int32_t& outX, int32_t& outY) {
	outX = 0;
	outY = 0;
	if (binding.empty()) {
		return;
	}

	// 軸2D(まとめて1つの名前)。
	if (binding == "WASD") {
		ReadKeyPad(DIK_W, DIK_S, DIK_A, DIK_D, outX, outY);
		return;
	}
	if (binding == "Arrows") {
		ReadKeyPad(DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT, outX, outY);
		return;
	}
	if (binding == "LeftStick" || binding == "RightStick") {
		const Vector2 stick = (binding == "LeftStick") ? Input::GetLeftStick() : Input::GetRightStick();
		outX = Quantize(stick.x);
		outY = Quantize(stick.y);
		return;
	}
	if (binding == "LeftTrigger" || binding == "RightTrigger") {
		outX = Quantize((binding == "LeftTrigger") ? Input::GetLeftTrigger() : Input::GetRightTrigger());
		return;
	}

	// 軸1D: "マイナス側:プラス側"(例 "A:D")。
	if (const size_t separator = binding.find(':'); separator != std::string::npos) {
		int32_t minusValue = 0;
		int32_t plusValue = 0;
		ReadButton(binding.substr(0, separator), minusValue);
		ReadButton(binding.substr(separator + 1), plusValue);
		outX = ((plusValue > 0) ? kActionAxisMax : 0) - ((minusValue > 0) ? kActionAxisMax : 0);
		return;
	}

	// ボタン1つ。
	ReadButton(binding, outX);
}

void InputActionAsset::ReadAction(const InputActionDef& action, int32_t& outX, int32_t& outY) {
	outX = 0;
	outY = 0;
	// 複数の割り当ては、いちばん強く入っているものを使う(キーボードとパッドを同時に付けてもよい)。
	for (const std::string& binding : action.bindings) {
		int32_t x = 0;
		int32_t y = 0;
		ReadBinding(binding, x, y);
		if (std::abs(x) > std::abs(outX)) {
			outX = x;
		}
		if (std::abs(y) > std::abs(outY)) {
			outY = y;
		}
	}
	if (action.type == InputActionType::Button) {
		outX = (outX != 0) ? kActionAxisMax : 0;
		outY = 0;
	} else if (action.type == InputActionType::Axis1D) {
		outY = 0;
	}
}

} // namespace KujataEngine
