#pragma once

#include "../input/InputActions.h"
#include "../math/Vector2.h"
#include "KujataApi.h"
#include "SimCommand.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace KujataEngine {

/// <summary>
/// 今のアクションの値(プレイヤー1人分)。**ゲームのコードはここだけを読む**(Input:: を直接呼ばない)。
///
/// 値はコマンド(ActionCommand)でしか変わらない。誰が押したか(キーボード・AI・CUI・リプレイ)に関係なく
/// 同じ形になるので、遊んだ操作をそのまま記録・再生できる。
///
/// Pressed / Released は「1つ前の更新の値」と比べて決まる(BeginStep で写す)。
/// </summary>
class ActionState {
public:
	/// <summary>1つ前の更新の値として、今の値を写す(更新の頭で呼ぶ)。</summary>
	KUJATA_API void BeginStep();

	/// <summary>値を入れる(ActionCommand::Execute から呼ばれる)。x, y は -kActionAxisMax 〜 kActionAxisMax。</summary>
	KUJATA_API void SetValue(const std::string& name, int32_t x, int32_t y);

	/// <summary>押しているか(ボタン)/ 何か入っているか(軸)。</summary>
	KUJATA_API bool Held(const std::string& name) const;

	/// <summary>この更新で押されたか(0 → 1 に変わった)。</summary>
	KUJATA_API bool Pressed(const std::string& name) const;

	/// <summary>この更新で離されたか(1 → 0 に変わった)。</summary>
	KUJATA_API bool Released(const std::string& name) const;

	/// <summary>-1 〜 1 に戻した値(ボタンは 0 か 1)。</summary>
	KUJATA_API float Axis1D(const std::string& name) const;

	KUJATA_API Vector2 Axis2D(const std::string& name) const;

	/// <summary>量子化したままの値(リプレイ・ハッシュ用)。</summary>
	KUJATA_API int32_t RawX(const std::string& name) const;
	KUJATA_API int32_t RawY(const std::string& name) const;

	/// <summary>全部 0 に戻す(Play の開始・終了時)。</summary>
	KUJATA_API void Clear();

	/// <summary>今値が入っているアクションの名前(CUI の表示用)。</summary>
	KUJATA_API std::vector<std::string> GetNames() const;

private:
	struct Value {
		int32_t x = 0;
		int32_t y = 0;
	};

	Value Find(const std::unordered_map<std::string, Value>& values, const std::string& name) const;

	std::unordered_map<std::string, Value> current_;
	std::unordered_map<std::string, Value> previous_;
};

/// <summary>
/// アクションの値を1つ変えるコマンド。キーボード・AI・CUI・リプレイのどれからでも、これを積む。
/// </summary>
class ActionCommand : public SimCommand {
public:
	ActionCommand() = default;
	KUJATA_API ActionCommand(std::string name, int32_t x, int32_t y);

	const char* GetType() const override { return "Action"; }
	KUJATA_API void Execute(SimContext& context) override;
	KUJATA_API void Write(nlohmann::json& out) const override;
	KUJATA_API void Read(const nlohmann::json& in) override;

	const std::string& GetName() const { return name_; }
	int32_t GetX() const { return x_; }
	int32_t GetY() const { return y_; }

private:
	std::string name_;
	int32_t x_ = 0;
	int32_t y_ = 0;
};

} // namespace KujataEngine
