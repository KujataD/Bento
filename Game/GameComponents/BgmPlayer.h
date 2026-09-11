#pragma once
#include <KujataEngine.h>
#include <string>

/// <summary>
/// シーンのBGMを鳴らす係。**1シーンに1つ**置く。
///
/// エンジンの AudioSourceComponent ではなく [[GameAudio]] を通すのは、
/// 音量設定([[GameSettings]])を一箇所で効かせるためと、
/// **シーンを跨いで前の曲を確実に止める**ため(AudioSourceはシーンごとに別インスタンスなので、
/// 切り替え時に前の曲が鳴り残ることがある)。
/// </summary>
class BgmPlayer : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "BgmPlayer"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void OnPlayStop() override;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(bgmPath_, "Bgm Path",
		    "Data相対の音声パス(例: Audio/bg_Title.mp3)。mp3もwavも読める。空なら何も鳴らさない。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(stopOnExit_, "Stop On Exit",
		    "シーンを抜けるときに止めるか。**offにすると次のシーンへ曲が続く**\n"
		    "(タイトル→キャラ選択のように曲を繋げたい場合に使う)。");
	}

	KUJATA_FIELD_STRING(bgmPath_, "");
	KUJATA_FIELD_BOOL(stopOnExit_, true);
};
