#pragma once

#include <string>

namespace KujataEngine {

/// <summary>
/// ゲームのプロジェクト(`GameModule/GameModule.vcxproj` と `.filters`)に、プロジェクトの中の .cpp / .h を並べ直す。
/// ワイルドカードだと Visual Studio が読み直すまで一覧に出ないので、ファイルを 1 つずつ書く。
/// フィルターはフォルダと同じ階層にする。Data・Temp・GameModule/bin の中は入れない。
/// 中身が変わったときだけ書く(Visual Studio の「再読み込み」を余計に出さないため)。
/// スクリプトの作成・DLL の読み直し・エディタの起動のときに呼ばれる。CUI は project.sync。
/// </summary>
namespace GameProjectSync {

struct Result {
	bool succeeded = false;
	// ファイルを書き直したか。
	bool changed = false;
	int sourceCount = 0;
	std::string message;
};

Result Sync();

} // namespace GameProjectSync

} // namespace KujataEngine
