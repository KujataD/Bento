#pragma once
#include <string>

/// <summary>
/// キャラ選択シーンの結果(操作キャラ名)をシーンをまたいで受け渡す置き場。
/// CharacterSelectManagerがSetし、次シーンのPartyManagerがConsumeで一度だけ読む。
/// Consume方式なのは、エディタからSampleSceneを直接Playしたときに
/// 前回Playの選択が残ってInspector設定を上書きしないようにするため。
/// </summary>
namespace PartySelection {

inline std::string& LeaderNameRef() {
	static std::string leaderName;
	return leaderName;
}

inline void SetLeaderName(const std::string& name) {
	LeaderNameRef() = name;
}

/// <summary>選択されたリーダー名を取り出してクリアする(未選択なら空文字)。</summary>
inline std::string ConsumeLeaderName() {
	std::string name = LeaderNameRef();
	LeaderNameRef().clear();
	return name;
}

} // namespace PartySelection
