#pragma once

#include "KujataApi.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace KujataEngine {

class Scene;
class ActionState;

/// <summary>
/// コマンドが触ってよいもの(シムの持ち物)。今は Scene とアクションの状態だけ。
/// 決定論の土台(ティック・乱数)を入れるときに、ここへ SimClock / SimRandom を足す(.claude/tick-replay.md §7)。
/// </summary>
struct SimContext {
	Scene* scene = nullptr;
	ActionState* actions = nullptr; // プレイヤー0のアクション(複数人は後で配列にする)
};

/// <summary>
/// シムを動かす要求1件(コマンドパターン)。**ゲームの状態を変えるものは、すべてこれを通す**。
/// 入口が「キー入力・AI・CUI・リプレイ」のどれでも同じ形になるので、
/// 後からリプレイ(.krp)へそのまま書き出せる(.claude/tick-replay.md §5)。
/// </summary>
class SimCommand {
public:
	virtual ~SimCommand() = default;

	/// <summary>ファイルに書く型名。SimCommandFactory で作り直すときの鍵。</summary>
	virtual const char* GetType() const = 0;

	/// <summary>更新の頭で呼ばれる(将来はティックの頭)。</summary>
	virtual void Execute(SimContext& context) = 0;

	/// <summary>リプレイへ書く。</summary>
	virtual void Write(nlohmann::json& out) const = 0;

	/// <summary>リプレイから読む。</summary>
	virtual void Read(const nlohmann::json& in) = 0;
};

/// <summary>
/// コマンドの型を名前から作り直す入れ物(ComponentFactory と同じ形)。
/// ゲーム(GameModule)も自分のコマンドを登録できる。
/// </summary>
class SimCommandFactory {
public:
	using CreateFunc = std::function<std::unique_ptr<SimCommand>()>;

	static KUJATA_API SimCommandFactory& GetInstance();

	KUJATA_API void Register(const std::string& typeName, CreateFunc createFunc);

	/// <summary>型名からコマンドを作る(未登録なら nullptr)。</summary>
	KUJATA_API std::unique_ptr<SimCommand> Create(const std::string& typeName) const;

	KUJATA_API bool IsRegistered(const std::string& typeName) const;

private:
	SimCommandFactory() = default;
	std::unordered_map<std::string, CreateFunc> creators_;
};

/// <summary>コマンドの出どころ。1ティックの中の実行順は「出どころの番号 → 積まれた順」で固定する。</summary>
enum : uint32_t {
	kCommandSourceEditor = 0,       // CUI・Inspector・リプレイの編集コマンド
	kCommandSourceLocalPlayer = 1,  // 手元のキーボード・パッド
	kCommandSourceAgentBase = 100,  // AI エージェント(100 + エージェント番号)
};

} // namespace KujataEngine
