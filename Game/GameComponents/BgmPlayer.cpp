#include "BgmPlayer.h"

#include "GameAudio.h"

using namespace KujataEngine;

void BgmPlayer::OnPlayStart() {
	// **効果音は先に読んでおく。** 初めて敵に当てた瞬間にWAVを読みに行くと、
	// 一番手応えが要る場面でカクつく。BGMを鳴らすシーンなら必ずSEも使うので、ここで賄う。
	GameAudio::PreloadSe();

	if (bgmPath_.empty()) {
		return;
	}
	GameAudio::PlayBgm(bgmPath_);
}

void BgmPlayer::OnPlayStop() {
	if (stopOnExit_) {
		GameAudio::StopBgm();
	}
}
