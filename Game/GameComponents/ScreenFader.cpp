#include "ScreenFader.h"

#include "GameSession.h"

#include <components/CanvasComponent.h>
#include <components/ImageComponent.h>
#include <components/RectTransformComponent.h>

using namespace KujataEngine;

namespace {
// 実行時に作る覆いの名前。`__`始まりで「手で置いたものではない」ことを示す。
constexpr const char* kFadeCanvasName = "__ScreenFadeCanvas";
constexpr const char* kFadeImageName = "__ScreenFadeImage";
// 暗転しきったまま切り替わらなかったときに、諦めて明転するまでの秒数。
constexpr float kStuckBlackGiveUpSeconds = 4.0f;
} // namespace

void ScreenFader::OnPlayStart() {
	// Playインスタンスはコンポーネントを使い回すので、前回Playで作った覆いのポインタは必ず捨てる
	// (Stopでシーンが復元され、GameObjectは作り直されている)。
	fadeImage_ = nullptr;
	fadeCanvasObject_ = nullptr;
	hasPendingTransition_ = false;
	pendingScene_.clear();
	pendingElapsed_ = 0.0f;

	if (fadeInOnStart_) {
		alpha_ = 1.0f;
		targetAlpha_ = 0.0f;
		fadeSpeed_ = (fadeInSeconds_ > 0.0f) ? (1.0f / fadeInSeconds_) : 0.0f;
	} else {
		alpha_ = 0.0f;
		targetAlpha_ = 0.0f;
		fadeSpeed_ = 0.0f;
	}
}

void ScreenFader::Update() {
	if (!fadeImage_) {
		CreateOverlay();
		if (!fadeImage_) {
			return;
		}
	}

	if (alpha_ != targetAlpha_) {
		if (fadeSpeed_ <= 0.0f) {
			alpha_ = targetAlpha_;
		} else {
			// ポーズ(timeScale=0)中でもフェードは進める必要があるので実時間で数える。
			const float step = fadeSpeed_ * Time::GetUnscaledDeltaTime();
			if (alpha_ < targetAlpha_) {
				alpha_ = (alpha_ + step > targetAlpha_) ? targetAlpha_ : alpha_ + step;
			} else {
				alpha_ = (alpha_ - step < targetAlpha_) ? targetAlpha_ : alpha_ - step;
			}
		}
	}
	ApplyAlpha();

	// **完全に黒くなってから切り替える**。ChangeSceneは同期ブロッキングなので、
	// ここで起きる停止が真っ黒の中に隠れて見えなくなる。
	if (hasPendingTransition_ && IsOpaque()) {
		hasPendingTransition_ = false;
		const std::string target = pendingScene_;
		pendingScene_.clear();
		if (pendingViaLoading_) {
			GameSession::LoadSceneWithLoading(target);
		} else {
			GameSession::LoadSceneImmediate(target);
		}
	}

	// 黒いまま取り残されないための保険。
	// 切り替えが成功すればこのComponentごとシーンが作り直されるので、ここへは来ない。
	// 逆に `ChangeScene` が握り潰される(Prefab編集中 / GameModule未ロード / シーン名の綴り違い)と
	// **画面が真っ暗のまま操作不能**になるため、一定時間で明転して復帰させる。
	if (IsOpaque() && !hasPendingTransition_) {
		pendingElapsed_ += Time::GetUnscaledDeltaTime();
		if (pendingElapsed_ > kStuckBlackGiveUpSeconds) {
			pendingElapsed_ = 0.0f;
			FadeIn(transitionFadeSeconds_);
		}
	} else {
		pendingElapsed_ = 0.0f;
	}
}

void ScreenFader::FadeOut(float seconds) {
	targetAlpha_ = 1.0f;
	fadeSpeed_ = (seconds > 0.0f) ? (1.0f / seconds) : 0.0f;
}

void ScreenFader::FadeIn(float seconds) {
	targetAlpha_ = 0.0f;
	fadeSpeed_ = (seconds > 0.0f) ? (1.0f / seconds) : 0.0f;
}

void ScreenFader::SetAlphaImmediate(float alpha) {
	alpha_ = alpha;
	targetAlpha_ = alpha;
	fadeSpeed_ = 0.0f;
	ApplyAlpha();
}

void ScreenFader::TransitionTo(const std::string& sceneName, bool viaLoading, float fadeSeconds) {
	if (sceneName.empty() || hasPendingTransition_) {
		return; // 連打で二重に予約しない。
	}
	hasPendingTransition_ = true;
	pendingElapsed_ = 0.0f;
	pendingViaLoading_ = viaLoading;
	pendingScene_ = sceneName;
	FadeOut((fadeSeconds > 0.0f) ? fadeSeconds : transitionFadeSeconds_);
}

ScreenFader* ScreenFader::FindInScene(Scene* scene) {
	if (!scene) {
		return nullptr;
	}
	for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (ScreenFader* fader = object->GetComponent<ScreenFader>()) {
			return fader;
		}
	}
	return nullptr;
}

void ScreenFader::RequestTransition(Scene* scene, const std::string& sceneName, bool viaLoading, float fadeSeconds) {
	if (sceneName.empty()) {
		return;
	}
	if (ScreenFader* fader = FindInScene(scene)) {
		fader->TransitionTo(sceneName, viaLoading, fadeSeconds);
		return;
	}
	// Faderを置いていないシーンでも遷移だけは成立させる(暗転が無いだけ)。
	if (viaLoading) {
		GameSession::LoadSceneWithLoading(sceneName);
	} else {
		GameSession::LoadSceneImmediate(sceneName);
	}
}

void ScreenFader::CreateOverlay() {
	if (!owner_ || !owner_->GetScene()) {
		return;
	}
	Scene* scene = owner_->GetScene();

	// 覆いのCanvas(Screen Space - Overlayが既定)。他のCanvasより手前に出す。
	GameObject* canvasObject = scene->CreateGameObject(kFadeCanvasName);
	if (!canvasObject) {
		return;
	}
	fadeCanvasObject_ = canvasObject;
	if (CanvasComponent* canvas = canvasObject->AddComponent<CanvasComponent>()) {
		canvas->SetSortOrder(sortOrder_);
	}

	// 全画面へ引き伸ばした1枚のImage(Stretch All相当)。
	GameObject* imageObject = scene->CreateGameObject(kFadeImageName);
	if (!imageObject) {
		return;
	}
	imageObject->SetParent(canvasObject);
	if (RectTransformComponent* rect = imageObject->AddComponent<RectTransformComponent>()) {
		rect->SetAnchorMin({0.0f, 0.0f});
		rect->SetAnchorMax({1.0f, 1.0f});
		rect->SetPivot({0.5f, 0.5f});
		rect->SetAnchoredPosition({0.0f, 0.0f});
		rect->SetSizeDelta({0.0f, 0.0f});
	}
	fadeImage_ = imageObject->AddComponent<ImageComponent>();
	ApplyAlpha();
}

void ScreenFader::ApplyAlpha() {
	if (!fadeImage_) {
		return;
	}
	fadeImage_->SetColor({fadeColor_.x, fadeColor_.y, fadeColor_.z, alpha_});

	// 透明なときは覆いを描画対象から外す(完全に透明な全画面Imageを描く意味が無いため)。
	if (fadeCanvasObject_) {
		fadeCanvasObject_->SetActive(alpha_ > 0.001f);
	}
}
