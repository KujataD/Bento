#include "CommandQueue.h"

#include <algorithm>

namespace KujataEngine {

void CommandQueue::Push(uint32_t source, std::unique_ptr<SimCommand> command) {
	if (!command) {
		return;
	}
	pending_.push_back(Entry{source, nextOrder_++, std::move(command)});
}

size_t CommandQueue::ExecuteAll(SimContext& context) {
	// 出どころの番号 → 積まれた順。安定ソートなので、同じ操作なら毎回同じ順番になる。
	std::stable_sort(pending_.begin(), pending_.end(), [](const Entry& a, const Entry& b) {
		return (a.source != b.source) ? a.source < b.source : a.order < b.order;
	});

	for (Entry& entry : pending_) {
		if (entry.command) {
			entry.command->Execute(context);
		}
	}
	lastExecutedCount_ = pending_.size();
	pending_.clear();
	return lastExecutedCount_;
}

void CommandQueue::Clear() {
	pending_.clear();
	nextOrder_ = 0;
	lastExecutedCount_ = 0;
}

size_t CommandQueue::GetPendingCount() const { return pending_.size(); }

size_t CommandQueue::GetLastExecutedCount() const { return lastExecutedCount_; }

} // namespace KujataEngine
