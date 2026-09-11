#pragma once

#include <BahamutAI/AI.h>
#include <KujataEngine.h>

#include "Player.h"

/// <summary>
/// ボタンなどから指定シーンへ移動するだけの小さな進行役。
/// Via Loading がONなら、直接切り替えずローディング画面([LoadingScreen])を経由する。
/// </summary>
class ChangeSceneManager : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "ChangeSceneManager"; }

	// UnityのButton.onClick等から呼べるメソッドを公開する。
	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;
private:
	void OnClick();

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING(sceneName_);
		KUJATA_REGISTER_BOOL_NAMED_TIP(viaLoading_, "Via Loading",
		    "ローディング画面を挟むか。ONだと LoadingScene を経由して切り替える\n"
		    "(ChangeSceneは同期ブロッキングなので、暗転中に読ませてカクつきを隠す)。");
	}
	KUJATA_FIELD_STRING(sceneName_, "SampleScene");
	KUJATA_FIELD_BOOL(viaLoading_, true);
};
