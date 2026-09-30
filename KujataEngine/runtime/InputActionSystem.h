#pragma once

#include "../input/InputActions.h"
#include "ActionState.h"
#include "CommandQueue.h"
#include "KujataApi.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace KujataEngine {

class Scene;

/// <summary>
/// 操作を一括で管理する場所(Unity の Input System のアクションに当たる)。
///
/// 流れ:
///   1. 生の入力(Input::)を読むのは **CollectFromDevices の中だけ**。割り当ては InputActions.json で決める
///   2. 前の更新から**値が変わったアクションだけ** ActionCommand にして CommandQueue へ積む
///   3. Step() が「前の値を写す → コマンドを順に実行」して ActionState を更新する
///   4. ゲームのコードは ActionState(GetActions())だけを読む
///
/// キーボードのほかに、AI エージェント(PushAction)・CUI(action.set)・将来のリプレイも、
/// 同じ ActionCommand を積むだけで同じように動かせる。
/// </summary>
class InputActionSystem {
public:
	static KUJATA_API InputActionSystem* GetInstance();

	/// <summary>アクションの定義を読み直す(`Data/ProjectSettings/InputActions.json`。無ければ既定)。</summary>
	KUJATA_API bool Reload(std::string* outMessage = nullptr);

	KUJATA_API const std::vector<InputActionDef>& GetDefinitions();

	/// <summary>定義を置き換えて `InputActions.json` に保存する(エディタの編集用)。</summary>
	KUJATA_API bool SaveDefinitions(const std::vector<InputActionDef>& actions, std::string* outMessage = nullptr);

	/// <summary>アクションの今の値。**ゲームのコードはここを読む**。</summary>
	KUJATA_API ActionState& GetActions();

	KUJATA_API CommandQueue& GetQueue();

	/// <summary>
	/// アクションの値を変えるコマンドを積む(AI エージェント・CUI 用)。値は -1 〜 1。
	/// 実際に反映されるのは次の Step()。
	/// </summary>
	KUJATA_API void PushAction(const std::string& name, float x, float y = 0.0f, uint32_t source = kCommandSourceLocalPlayer);

	/// <summary>生の入力を読み、変わったアクションだけコマンドにする(Play 中に毎フレーム呼ぶ)。</summary>
	KUJATA_API void CollectFromDevices();

	/// <summary>コマンドを実行してアクションの値を更新する(Scene の更新の直前に呼ぶ)。</summary>
	KUJATA_API void Step(Scene* scene);

	/// <summary>アクションの値・積まれたコマンド・デバイスの記憶をすべて捨てる(Play の開始・終了時)。</summary>
	KUJATA_API void Reset();

	/// <summary>デバイスからの読み取りを止める(CUI や AI だけで動かして確かめたいとき)。</summary>
	KUJATA_API void SetDeviceInputEnabled(bool enabled);
	KUJATA_API bool IsDeviceInputEnabled() const;

	/// <summary>定義の読み込み元(表示用)。</summary>
	KUJATA_API const std::string& GetSourceDescription();

private:
	InputActionSystem() = default;
	void EnsureLoaded();

	std::vector<InputActionDef> actions_;
	// 1つ前に読んだデバイスの値(変わったときだけコマンドにするため)。
	std::unordered_map<std::string, std::pair<int32_t, int32_t>> lastDeviceValues_;
	ActionState actions0_;
	CommandQueue queue_;
	std::string sourceDescription_;
	bool loaded_ = false;
	bool deviceInputEnabled_ = true;
};

} // namespace KujataEngine
