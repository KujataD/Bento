#include "LoadingScreen.h"

#include "GameSession.h"
#include "ScreenFader.h"

#include <components/ImageComponent.h>
#include <components/RectTransformComponent.h>
#include <components/TextComponent.h>
#include <cmath>

using namespace KujataEngine;

namespace {
// 実ロードの前に見せる進捗の上限。100%にしてから待たせると「終わっているのに進まない」に見える。
constexpr float kHoldProgressCap = 0.9f;
} // namespace

void LoadingScreen::OnPlayStart() {
	// 直前のシーンがポーズ(死亡メニュー)で止めたままかもしれないので、必ず戻す。
	// **戻し忘れると遷移先のゲームがtimeScale=0のまま始まる。**
	Time::SetTimeScale(1.0f);

	state_ = State::FadeIn;
	elapsed_ = 0.0f;
	totalElapsed_ = 0.0f;
	progress_ = 0.0f;
	spinnerAngle_ = 0.0f;
	lastDotCount_ = -1;
	fader_ = nullptr;
	progressFill_ = nullptr;
	spinner_ = nullptr;
	statusText_ = nullptr;

	targetScene_ = GameSession::ConsumeNextScene();
	if (targetScene_.empty()) {
		targetScene_ = fallbackScene_;
	}
}

void LoadingScreen::Update() {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene) {
		return;
	}
	if (!fader_) {
		fader_ = ScreenFader::FindInScene(scene);
	}

	// ローディング自体はゲーム内時間に左右されない。必ず実時間で進める。
	const float deltaTime = Time::GetUnscaledDeltaTime();
	elapsed_ += deltaTime;
	totalElapsed_ += deltaTime;
	spinnerAngle_ += spinnerSpeed_ * deltaTime;

	switch (state_) {
	case State::FadeIn:
		// ScreenFaderが自前でフェードインしているので、明けきるのを待つだけ。
		if (!fader_ || fader_->IsClear() || elapsed_ >= fadeInSeconds_) {
			state_ = State::Hold;
			elapsed_ = 0.0f;
		}
		break;

	case State::Hold: {
		if (!indeterminate_) {
			// それらしい進捗。実際の重さは分からないので、頭打ちのある指数カーブで演出する。
			const float tau = (progressTau_ > 0.0001f) ? progressTau_ : 0.0001f;
			progress_ = kHoldProgressCap * (1.0f - std::exp(-elapsed_ / tau));
		}
		if (elapsed_ >= minDisplaySeconds_) {
			if (!indeterminate_) {
				progress_ = 1.0f;
			}
			state_ = State::Complete;
			elapsed_ = 0.0f;
		}
		break;
	}

	case State::Complete:
		if (!indeterminate_) {
			progress_ = 1.0f;
		}
		// **割合を名乗らないなら「満ちるのを見せる間」も要らない。**
		// 満ちた絵で待たせるのが、そのまま「100%で固まっている」に見える原因だった。
		if (indeterminate_ || elapsed_ >= completeHoldSeconds_) {
			state_ = State::Done;
			// **暗転させずにここで読む。** ChangeSceneは同期ブロッキングでフレームが数秒止まるので、
			// 止まっている絵は黒ではなくローディング画面でなければならない
			// (黒で止めるとフリーズと見分けが付かない)。遷移先は自前のScreenFaderで黒から明ける。
			GameSession::LoadSceneImmediate(targetScene_);
		}
		break;

	case State::Done:
		break;
	}

	UpdateVisuals();
}

void LoadingScreen::UpdateVisuals() {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene) {
		return;
	}

	if (!progressFill_ && !progressFillName_.empty()) {
		if (GameObject* object = scene->FindGameObjectByName(progressFillName_)) {
			progressFill_ = object->GetComponent<ImageComponent>();
		}
	}
	if (progressFill_) {
		// **割合を名乗らないときは繰り返し流すだけ。** どこまで進んだかは言わず、
		// 「動いている」ことだけを伝える。読み込みでフレームが止まればここも止まるが、
		// そのときは満杯ではなく途中の絵で止まるので「終わったのに固まっている」とは見えない。
		if (indeterminate_) {
			const float period = (sweepSeconds_ > 0.01f) ? sweepSeconds_ : 0.01f;
			progress_ = std::fmod(totalElapsed_, period) / period;
		}
		progressFill_->SetFillAmount(progress_);
	}

	if (!statusText_ && !statusTextName_.empty()) {
		if (GameObject* object = scene->FindGameObjectByName(statusTextName_)) {
			statusText_ = object->GetComponent<TextComponent>();
		}
	}
	if (statusText_ && !statusLabel_.empty()) {
		const float interval = (dotIntervalSeconds_ > 0.01f) ? dotIntervalSeconds_ : 0.01f;
		const int dotCount = static_cast<int>(totalElapsed_ / interval) % 4;
		// 毎フレーム同じ文字列を流し込むとアトラスを無駄に触るので、変わったときだけ書く。
		if (dotCount != lastDotCount_) {
			lastDotCount_ = dotCount;
			statusText_->SetText(statusLabel_ + std::string(static_cast<std::size_t>(dotCount), '.'));
		}
	}

	if (!spinner_ && !spinnerName_.empty()) {
		if (GameObject* object = scene->FindGameObjectByName(spinnerName_)) {
			spinner_ = object->GetComponent<RectTransformComponent>();
		}
	}
	if (spinner_) {
		spinner_->SetRotationZ(spinnerAngle_);
	}
}
