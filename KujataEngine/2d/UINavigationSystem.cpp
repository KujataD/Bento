#include "UINavigationSystem.h"

#include "../base/Time.h"
#include "../components/ButtonComponent.h"
#include "../components/CanvasComponent.h"
#include "../components/RectTransformComponent.h"
#include "../input/Input.h"
#include "../runtime/UIInput.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include "UIRect.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace KujataEngine {
namespace {

// --- 入力の割り当て(変えるならここだけ) ---
constexpr WORD kSubmitButton = XINPUT_GAMEPAD_A;
constexpr WORD kCancelButton = XINPUT_GAMEPAD_B;
// スティックをこの値以上倒したら「その方向へ入力した」とみなす。
constexpr float kStickThreshold = 0.5f;
// 倒しっぱなしのときの、最初の1回から次までの待ち時間[s]と、以降の間隔[s]。
constexpr float kRepeatDelay = 0.40f;
constexpr float kRepeatInterval = 0.12f;

/// <summary>ナビゲーションの移動先候補(操作可能で、ナビ対象のボタン)。</summary>
struct NavCandidate {
	GameObject* object = nullptr;
	ButtonComponent* button = nullptr;
	// 矩形の中心(キャンバス単位)。1枚のCanvas内でしか比較しないのでscaleFactorは掛けなくてよい。
	float centerX = 0.0f;
	float centerY = 0.0f;
};

// --- フレームをまたぐ状態 ---
// 方向入力のリピート管理。
bool gHasDirection = false;
UINavDirection gLastDirection = UINavDirection::Up;
float gRepeatTimer = 0.0f;
// 起動直後にコントローラーが挿さっていればFocusモードで始める(パッド前提のゲームのため)。
bool gInitialized = false;

/// <summary>ノードを再帰し、ナビゲーション候補を集める。矩形計算はUIEventSystemと同じ手順。</summary>
void CollectNavCandidates(GameObject* node, const UIRect& parentRect, std::vector<NavCandidate>& out) {
	if (!node || !node->IsActive()) {
		return;
	}
	RectTransformComponent* rectTransform = node->GetComponent<RectTransformComponent>();
	if (!rectTransform) {
		return;
	}
	const UIRect rect = rectTransform->ComputeRect(parentRect);

	ButtonComponent* button = node->GetComponent<ButtonComponent>();
	if (button && button->IsEnabled() && button->IsInteractable() && button->GetNavigationMode() != ButtonComponent::NavigationMode::None) {
		NavCandidate candidate;
		candidate.object = node;
		candidate.button = button;
		candidate.centerX = rect.x + rect.width * 0.5f;
		candidate.centerY = rect.y + rect.height * 0.5f;
		out.push_back(candidate);
	}

	for (GameObject* child : node->GetChildren()) {
		CollectNavCandidates(child, rect, out);
	}
}

/// <summary>候補の中からobjectに一致するものを探す(見つからなければnullptr)。</summary>
const NavCandidate* FindCandidate(const std::vector<NavCandidate>& candidates, const GameObject* object) {
	if (!object) {
		return nullptr;
	}
	for (const NavCandidate& candidate : candidates) {
		if (candidate.object == object) {
			return &candidate;
		}
	}
	return nullptr;
}

/// <summary>
/// 今フレームの方向入力を1つに畳む。スティック・十字キー・矢印キー・WASDを合成し、
/// 縦横が同時に入ったときは倒しの強い方(同点なら縦)を採る。
/// </summary>
bool ReadDirectionInput(UINavDirection& outDirection) {
	const Vector2 stick = Input::GetLeftStick();
	float axisX = (std::fabs(stick.x) >= kStickThreshold) ? stick.x : 0.0f;
	// スティックのyは上が正。ここでは「上向き入力の強さ」として扱い、最後にUI座標(Y下向き)へ直す。
	float axisY = (std::fabs(stick.y) >= kStickThreshold) ? stick.y : 0.0f;

	// 十字キー/キーボードはデジタルなので±1として合成する。
	if (Input::GetControllerButton(XINPUT_GAMEPAD_DPAD_LEFT) || Input::GetKey(DIK_LEFT) || Input::GetKey(DIK_A)) {
		axisX = -1.0f;
	}
	if (Input::GetControllerButton(XINPUT_GAMEPAD_DPAD_RIGHT) || Input::GetKey(DIK_RIGHT) || Input::GetKey(DIK_D)) {
		axisX = 1.0f;
	}
	if (Input::GetControllerButton(XINPUT_GAMEPAD_DPAD_UP) || Input::GetKey(DIK_UP) || Input::GetKey(DIK_W)) {
		axisY = 1.0f;
	}
	if (Input::GetControllerButton(XINPUT_GAMEPAD_DPAD_DOWN) || Input::GetKey(DIK_DOWN) || Input::GetKey(DIK_S)) {
		axisY = -1.0f;
	}

	if (axisX == 0.0f && axisY == 0.0f) {
		return false;
	}
	if (axisY != 0.0f && std::fabs(axisY) >= std::fabs(axisX)) {
		outDirection = (axisY > 0.0f) ? UINavDirection::Up : UINavDirection::Down;
	} else {
		outDirection = (axisX > 0.0f) ? UINavDirection::Right : UINavDirection::Left;
	}
	return true;
}

bool IsSubmitTriggered() {
	return Input::GetControllerButtonTrigger(kSubmitButton) || Input::GetKeyTrigger(DIK_RETURN) || Input::GetKeyTrigger(DIK_SPACE);
}

bool IsSubmitHeld() {
	return Input::GetControllerButton(kSubmitButton) || Input::GetKey(DIK_RETURN) || Input::GetKey(DIK_SPACE);
}

bool IsCancelTriggered() {
	return Input::GetControllerButtonTrigger(kCancelButton) || Input::GetKeyTrigger(DIK_ESCAPE) || Input::GetKeyTrigger(DIK_BACK);
}

/// <summary>
/// directionの方向にある最も自然な移動先を返す(UnityのAutomatic Navigation相当)。
/// その方向に候補が無ければ、反対方向の一番遠い候補へ折り返す(縦メニューの端で上下がループする)。
/// </summary>
GameObject* FindNeighborAutomatic(const std::vector<NavCandidate>& candidates, const NavCandidate& current, UINavDirection direction) {
	// UI座標はY下向き。Upは-Y、Downは+Y。
	float dirX = 0.0f;
	float dirY = 0.0f;
	switch (direction) {
	case UINavDirection::Up:
		dirY = -1.0f;
		break;
	case UINavDirection::Down:
		dirY = 1.0f;
		break;
	case UINavDirection::Left:
		dirX = -1.0f;
		break;
	case UINavDirection::Right:
		dirX = 1.0f;
		break;
	}
	// 進行方向に直交する軸(横ずれの量を測る)。
	const float perpX = -dirY;
	const float perpY = dirX;

	GameObject* best = nullptr;
	float bestScore = (std::numeric_limits<float>::max)();
	GameObject* wrapBest = nullptr;
	float wrapBestScore = (std::numeric_limits<float>::max)();

	for (const NavCandidate& candidate : candidates) {
		if (candidate.object == current.object) {
			continue;
		}
		const float deltaX = candidate.centerX - current.centerX;
		const float deltaY = candidate.centerY - current.centerY;
		const float projection = deltaX * dirX + deltaY * dirY;                 // 進行方向の距離(負なら反対側)
		const float perpendicular = std::fabs(deltaX * perpX + deltaY * perpY); // 横ずれ

		// 斜め過ぎる候補は除外する(真横のボタンへ上入力で飛ばないように)。
		if (perpendicular > std::fabs(projection) * 2.0f) {
			continue;
		}
		// 近く・正面ほど小さくなるスコア。横ずれを重めに罰する。
		const float score = projection + perpendicular * 3.0f;

		if (projection > 1.0f) {
			if (score < bestScore) {
				bestScore = score;
				best = candidate.object;
			}
		} else if (projection < -1.0f) {
			// 折り返し用: 反対方向で最も遠い(= scoreが最も小さい)ものを控えておく。
			if (score < wrapBestScore) {
				wrapBestScore = score;
				wrapBest = candidate.object;
			}
		}
	}

	return best ? best : wrapBest;
}

} // namespace

bool UpdateUINavigation(Scene& scene, float targetWidth, float targetHeight) {
	if (targetWidth <= 0.0f || targetHeight <= 0.0f) {
		return false;
	}

	if (!gInitialized) {
		gInitialized = true;
		// パッド前提のゲームなので、コントローラーが挿さっていれば最初からフォーカス操作で始める。
		if (Input::IsControllerConnected(0)) {
			SetUIInputMode(UIInputMode::Focus);
		}
	}

	// --- 対象Canvasを決める: Overlayかつ操作可能なボタンを持つ、最前面(sortOrder最大)の1枚 ---
	std::vector<std::pair<int, GameObject*>> canvases;
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		CanvasComponent* canvas = gameObject->GetComponent<CanvasComponent>();
		if (!canvas || !canvas->IsEnabled() || canvas->IsWorldSpace()) {
			continue;
		}
		canvases.emplace_back(canvas->GetSortOrder(), gameObject.get());
	}
	std::stable_sort(canvases.begin(), canvases.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

	std::vector<NavCandidate> candidates;
	CanvasComponent* activeCanvas = nullptr;
	for (auto it = canvases.rbegin(); it != canvases.rend(); ++it) {
		GameObject* canvasObject = it->second;
		CanvasComponent* canvas = canvasObject->GetComponent<CanvasComponent>();
		const CanvasComponent::Layout layout = canvas->GetLayout(targetWidth, targetHeight);
		const UIRect rootRect{0.0f, 0.0f, layout.canvasWidth, layout.canvasHeight};

		std::vector<NavCandidate> found;
		for (GameObject* child : canvasObject->GetChildren()) {
			CollectNavCandidates(child, rootRect, found);
		}
		if (!found.empty()) {
			candidates = std::move(found);
			activeCanvas = canvas;
			break; // 最前面のメニューだけがフォーカスを受ける(背後のCanvasへは漏らさない)。
		}
	}

	if (candidates.empty()) {
		// 操作対象が無い(通常のゲーム中のHUDなど)。入力モードには触れないので、
		// ゲーム側のAボタン(回避)などをここが横取りすることはない。
		gHasDirection = false;
		gRepeatTimer = 0.0f;
		SetUISelected(nullptr);
		return false;
	}

	// --- 入力を読む。何か操作されたらフォーカスモードへ切り替える ---
	UINavDirection direction = UINavDirection::Up;
	const bool hasDirection = ReadDirectionInput(direction);
	const bool submitTriggered = IsSubmitTriggered();
	const bool cancelTriggered = IsCancelTriggered();
	if (hasDirection || submitTriggered || cancelTriggered) {
		SetUIInputMode(UIInputMode::Focus);
	}

	if (GetUIInputMode() != UIInputMode::Focus) {
		return false; // マウス操作中。ポインタ側に任せる。
	}

	// --- 選択の検証と初期化 ---
	// GetUISelectedはシーン破棄でぶら下がる可能性があるため、必ず今フレームの候補と突き合わせてから使う。
	const NavCandidate* current = FindCandidate(candidates, GetUISelected());
	if (!current) {
		const NavCandidate* first = activeCanvas ? FindCandidate(candidates, activeCanvas->GetFirstSelected()) : nullptr;
		current = first ? first : &candidates.front();
		SetUISelected(current->object);
	}

	// --- 方向移動(押しっぱなしでリピート) ---
	bool shouldMove = false;
	if (!hasDirection) {
		gHasDirection = false;
		gRepeatTimer = 0.0f;
	} else if (!gHasDirection || direction != gLastDirection) {
		gHasDirection = true;
		gLastDirection = direction;
		gRepeatTimer = kRepeatDelay;
		shouldMove = true;
	} else {
		// UIはポーズ中(timeScale=0)でも動く必要があるので実時間で数える。
		gRepeatTimer -= Time::GetUnscaledDeltaTime();
		if (gRepeatTimer <= 0.0f) {
			gRepeatTimer = kRepeatInterval;
			shouldMove = true;
		}
	}

	if (shouldMove) {
		GameObject* next = nullptr;
		if (current->button->GetNavigationMode() == ButtonComponent::NavigationMode::Explicit) {
			GameObject* explicitNext = current->button->GetExplicitNeighbor(direction);
			next = FindCandidate(candidates, explicitNext) ? explicitNext : nullptr;
		} else {
			next = FindNeighborAutomatic(candidates, *current, direction);
		}
		if (next) {
			SetUISelected(next);
			current = FindCandidate(candidates, next);
		}
	}

	// --- 決定・キャンセル ---
	// どちらもシーン切り替えを起こしうるので、この後でcandidatesを触らないよう最後に置く。
	if (cancelTriggered && activeCanvas) {
		activeCanvas->GetOnCancel().Invoke();
	}
	if (current) {
		// 見た目は先に確定させる(FireOnClickでシーンが差し替わるとcurrentが無効になるため)。
		current->button->ApplyVisualState(IsSubmitHeld() ? ButtonComponent::VisualState::Pressed : ButtonComponent::VisualState::Highlighted);
		if (submitTriggered) {
			current->button->FireOnClick();
		}
	}
	return true;
}

} // namespace KujataEngine
