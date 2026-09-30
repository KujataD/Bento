#pragma once

#include "../runtime/KujataApi.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace KujataEngine {

/// <summary>アクションの種類。値は int16 に量子化して扱う(決定論とリプレイのため)。</summary>
enum class InputActionType {
	Button, // 押している = kAxisMax / 離している = 0
	Axis1D, // -kAxisMax 〜 kAxisMax
	Axis2D, // x, y それぞれ -kAxisMax 〜 kAxisMax
};

/// <summary>アクションの値の最大(1.0 に当たる)。float のまま持たずに整数へ丸める。</summary>
constexpr int32_t kActionAxisMax = 32767;

/// <summary>
/// アクション1つの定義。どのキー・ボタンで動かすかは bindings に書く(ゲームのコードにキーコードを出さない)。
/// </summary>
struct InputActionDef {
	std::string name;
	InputActionType type = InputActionType::Button;
	// 割り当て。1つのアクションに複数書ける(キーボードとパッドの両対応など)。使える書き方は InputActions.cpp の一覧。
	std::vector<std::string> bindings;
};

/// <summary>
/// アクションの定義一覧(`Data/ProjectSettings/InputActions.json`)と、割り当ての読み取り。
///
/// 使える割り当ての書き方:
///   ボタン: "W" "Space" "Shift" "Enter" "MouseLeft" "PadA" "PadL1"〜"PadR3" "PadStart" …(キー名は InputActions.cpp の表)
///   軸2D:   "WASD" "Arrows" "LeftStick" "RightStick"
///   軸1D:   "LeftTrigger" "RightTrigger"、"A:D" のように「マイナス側:プラス側」でキーを2つ書く
/// </summary>
class InputActionAsset {
public:
	/// <summary>`Data/ProjectSettings/InputActions.json` の場所。</summary>
	static KUJATA_API std::filesystem::path GetDefaultPath();

	/// <summary>
	/// JSON を読み込む。ファイルが無ければ、よく使うアクション(Move / Jump / Attack …)の既定を入れて true を返す
	/// (プロジェクトのファイルは勝手に作らない)。
	/// </summary>
	static KUJATA_API bool Load(const std::filesystem::path& path, std::vector<InputActionDef>& outActions, std::string& outMessage);

	/// <summary>定義を JSON に書き出す(Load と同じ形)。</summary>
	static KUJATA_API bool Save(const std::filesystem::path& path, const std::vector<InputActionDef>& actions, std::string& outMessage);

	/// <summary>ファイルが無いときに使う既定のアクション。</summary>
	static KUJATA_API std::vector<InputActionDef> CreateDefault();

	/// <summary>その種類のアクションに付けられる割り当ての名前(エディタの選択肢)。軸1D の "A:D" の形は含まない。</summary>
	static KUJATA_API std::vector<std::string> GetBindingNames(InputActionType type);

	/// <summary>エディタに出す表示名(パッドは "PadL1 (LB)" のように Xbox の名前を付ける)。</summary>
	static KUJATA_API std::string GetBindingLabel(const std::string& binding);

	/// <summary>ボタン1つとして読める名前か(キー・マウス・パッド)。軸1D の "A:D" の左右にも使う。</summary>
	static KUJATA_API bool IsButtonName(const std::string& name);

	/// <summary>その種類のアクションに付けられる割り当てか。</summary>
	static KUJATA_API bool IsValidBinding(InputActionType type, const std::string& binding);

	static KUJATA_API const char* ToString(InputActionType type);
	static KUJATA_API bool TryParseType(const std::string& text, InputActionType& outType);

	/// <summary>
	/// 割り当て1つを今の入力から読む。押していなければ (0, 0)。値は -kActionAxisMax 〜 kActionAxisMax。
	/// 名前が分からない割り当ては (0, 0) を返す(綴りの間違いで落とさない)。
	/// </summary>
	static KUJATA_API void ReadBinding(const std::string& binding, int32_t& outX, int32_t& outY);

	/// <summary>アクション1つ分を、すべての割り当てから読む(いちばん強く入っているものを使う)。</summary>
	static KUJATA_API void ReadAction(const InputActionDef& action, int32_t& outX, int32_t& outY);
};

} // namespace KujataEngine
