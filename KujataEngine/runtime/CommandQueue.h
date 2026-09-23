#pragma once

#include "SimCommand.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace KujataEngine {

/// <summary>
/// これから実行するコマンドの列。更新の頭で、**出どころの番号 → 積まれた順**に実行する
/// (順番を固定しないと、同じ操作でも結果が変わってしまう)。
/// 将来リプレイを作るときは、実行したコマンドをそのままファイルへ書けばよい。
/// </summary>
class CommandQueue {
public:
	struct Entry {
		uint32_t source = kCommandSourceLocalPlayer; // 出どころ(kCommandSource*)
		uint64_t order = 0;                          // 積まれた順(同じ出どころの中の並び)
		std::unique_ptr<SimCommand> command;
	};

	KUJATA_API void Push(uint32_t source, std::unique_ptr<SimCommand> command);

	/// <summary>積まれたコマンドを順に実行して空にする。実行した数を返す。</summary>
	KUJATA_API size_t ExecuteAll(SimContext& context);

	/// <summary>実行せずに捨てる(Play の開始・終了時)。</summary>
	KUJATA_API void Clear();

	KUJATA_API size_t GetPendingCount() const;

	/// <summary>今フレームに実行したコマンドの数(CUI の表示・確認用)。</summary>
	KUJATA_API size_t GetLastExecutedCount() const;

private:
	std::vector<Entry> pending_;
	uint64_t nextOrder_ = 0;
	size_t lastExecutedCount_ = 0;
};

} // namespace KujataEngine
