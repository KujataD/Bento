#include "GameFlowManager.h"
#include "GameAudio.h"

#include "BossIntroCutscene.h"
#include "EnemyHealth.h"
#include "GameSession.h"
#include "PartyManager.h"
#include "PauseMenu.h"
#include "PlayerHealth.h"
#include "Phase2Cutscene.h"
#include "ScreenFader.h"

#include <components/ImageComponent.h>
#include <components/TextComponent.h>

#include <cstdio>
#include <filesystem>

using namespace KujataEngine;

void GameFlowManager::OnPlayStart() {
	// **前回Playの後始末**: コンポーネントは使い回されるので、非シリアライズの状態は必ず戻す。
	// とくにtimeScaleは戻さないと「2回目のPlayが止まったまま始まる」形で表面化する。
	Time::SetTimeScale(1.0f);
	state_ = State::Playing;
	timer_ = 0.0f;
	bossHealth_ = nullptr;
	fadeTargets_.clear();

	// 演出用オブジェクトは必ず伏せてから始める(エディタで表示したまま保存されていても事故らない)。
	SetObjectActive(deathMessageName_, false);
	SetObjectActive(clearMessageName_, false);
	SetObjectActive(retryMenuName_, false);
	SetObjectActive(resultMenuName_, false);
	battleSeconds_ = 0.0f;
}

GameFlowManager* GameFlowManager::FindInScene(Scene* scene) {
	if (!scene) {
		return nullptr;
	}
	for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (GameFlowManager* flow = object->GetComponent<GameFlowManager>()) {
			return flow;
		}
	}
	return nullptr;
}

void GameFlowManager::FillResultTexts() {
	// 分:秒。**ミリ秒までは出さない。** 記録を競う作りではないので、読みやすさを優先する。
	const int totalSeconds = static_cast<int>(battleSeconds_);
	const int minutes = totalSeconds / 60;
	const int seconds = totalSeconds % 60;
	char timeBuffer[32]{};
	std::snprintf(timeBuffer, sizeof(timeBuffer), "%d:%02d", minutes, seconds);
	SetText(resultTimeName_, timeBuffer);

	SetText(resultRetryName_, std::to_string(GameSession::RetryCountRef()));
}

void GameFlowManager::SetText(const std::string& objectName, const std::string& text) {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || objectName.empty()) {
		return;
	}
	GameObject* object = scene->FindGameObjectByName(objectName);
	if (!object) {
		return;
	}
	if (TextComponent* label = object->GetComponent<TextComponent>()) {
		label->SetText(text);
	}
}

void GameFlowManager::RegisterInvokableMethods(InvokableMethodRegistry& registry) {
	registry.Add("Retry", [this]() { Retry(); });
	registry.Add("ReturnToTitle", [this]() { ReturnToTitle(); });
	// **再開点を選べるようにする。** 2形態を通しで戦い直すか、
	// 第2形態だけをやり直すかは、そのときの目的(挑戦か・確認か)で変わる。
	registry.Add("RetryFromStart", [this]() { RetryFrom(firstPhaseSceneName_); });
	registry.Add("RetryFromPhase2", [this]() { RetryFrom(secondPhaseSceneName_); });
}

void GameFlowManager::RetryFrom(const std::string& sceneName) {
	if (state_ == State::Leaving) {
		return; // 連打で二重に切り替えない。
	}
	if (sceneName.empty()) {
		Retry();
		return;
	}
	Time::SetTimeScale(1.0f);
	++GameSession::RetryCountRef();
	state_ = State::Leaving;
	// Retryと同じく**予約**で切り替える。Update中に直接切り替えると自分ごと消えて落ちる。
	ScreenFader::RequestTransition(owner_ ? owner_->GetScene() : nullptr, sceneName, true);
}

void GameFlowManager::Update() {
	// 演出とメニューはゲームを止めても進む必要があるので、常に実時間で数える。
	const float deltaTime = Time::GetUnscaledDeltaTime();

	switch (state_) {
	case State::Playing:
		// 戦っている間だけ時計を進める。演出の時間は状態が Playing から外れるので自然に除かれるが、
		// **ポーズと開幕演出はstate_を変えない**ので、ここで明示的に止める。
		// 実時間で数えている以上、メニューを開いて放置した時間も、幕開けを眺めていた時間も乗ってしまう。
		if (!PauseMenu::IsScenePaused(owner_ ? owner_->GetScene() : nullptr) &&
		    !BossIntroCutscene::IsSceneIntroPlaying(owner_ ? owner_->GetScene() : nullptr)) {
			battleSeconds_ += deltaTime;
		}
		if (IsBossDefeated()) {
			// **削り切っても終わりとは限らない。**
			// 次の形態への繋ぎ(Phase2Cutscene)が置いてあるシーンでは、撃破ではなく演出へ渡す。
			// 勝敗はまだ付いていないので、戦績にも記録しない。
			if (Phase2Cutscene* cutscene = Phase2Cutscene::FindInScene(owner_ ? owner_->GetScene() : nullptr)) {
				cutscene->Begin();
				state_ = State::PhaseTransition;
				timer_ = 0.0f;
				break;
			}
			// 勝敗が決まった瞬間に戦闘のBGMを止め、撃破の曲へ差し替える。
			// **PlayBgmが前の曲を止めてから差し替える**ので、ここで止めるのは
			// 「同じフレームで確実に切れている」ことを読み手に見せるため。
			GameAudio::StopBgm();
			GameAudio::PlayBgm(GameAudio::kClearBgmPath);
			RecordOutcome(true);
			state_ = State::ClearDelay;
			timer_ = 0.0f;
		} else if (IsPartyWiped()) {
			GameAudio::StopBgm();
			GameAudio::PlaySe(GameAudio::Se::Death);
			RecordOutcome(false);
			state_ = State::DeathDelay;
			timer_ = 0.0f;
		}
		break;

	case State::DeathDelay:
		timer_ += deltaTime;
		if (timer_ >= deathDelaySeconds_) {
			CaptureFadeTargets(SetObjectActive(deathMessageName_, true));
			ApplyFadeRatio(0.0f);
			state_ = State::DeathMessage;
			timer_ = 0.0f;
		}
		break;

	case State::DeathMessage:
		timer_ += deltaTime;
		ApplyFadeRatio((messageFadeSeconds_ > 0.0f) ? (timer_ / messageFadeSeconds_) : 1.0f);
		if (timer_ >= deathMessageSeconds_) {
			// **暗転を挟んでからメニューへ差し替える。** 直に切り替えると、闘技場が映ったまま
			// メニューだけが唐突に載る。ScreenFaderの覆いはどのCanvasより手前(Sort Order 1000)なので、
			// 黒で覆いきってから中身を入れ替えれば、切り替わる瞬間は見えない。
			if (ScreenFader* fader = ScreenFader::FindInScene(owner_ ? owner_->GetScene() : nullptr)) {
				fader->FadeOut(deathFadeOutSeconds_);
			}
			state_ = State::DeathFadeOut;
			timer_ = 0.0f;
		}
		break;

	case State::DeathFadeOut: {
		timer_ += deltaTime;
		ScreenFader* fader = ScreenFader::FindInScene(owner_ ? owner_->GetScene() : nullptr);
		// Faderが無いシーンでも進めるよう、暗転しきった判定は「不透明」か「時間切れ」のどちらでも通す。
		const bool covered = !fader || fader->IsOpaque() || timer_ >= deathFadeOutSeconds_;
		if (!covered) {
			break;
		}
		// 覆いの裏で入れ替える。死亡文字は用が済んだので伏せる。
		SetObjectActive(deathMessageName_, false);
		SetObjectActive(retryMenuName_, true);
		if (fader) {
			fader->FadeIn(deathFadeInSeconds_);
		}
		state_ = State::GameOverMenu;
		timer_ = 0.0f;
		break;
	}

	case State::GameOverMenu:
		// **毎フレーム入れ直す**。致命の一撃のヒットストップなど、他のComponentが
		// timeScaleを1.0へ戻す経路があるため、1回設定するだけでは止め続けられない。
		Time::SetTimeScale(0.0f);
		// 自動計測中は人の手が要らないよう、少し置いてから自分でRetryを押す。
		if (autoRetry_) {
			timer_ += deltaTime;
			if (timer_ >= autoRetryDelay_) {
				Retry();
			}
		}
		break;

	case State::ClearDelay:
		timer_ += deltaTime;
		if (timer_ >= clearDelaySeconds_) {
			CaptureFadeTargets(SetObjectActive(clearMessageName_, true));
			ApplyFadeRatio(0.0f);
			state_ = State::ClearMessage;
			timer_ = 0.0f;
		}
		break;

	case State::ClearMessage:
		timer_ += deltaTime;
		ApplyFadeRatio((messageFadeSeconds_ > 0.0f) ? (timer_ / messageFadeSeconds_) : 1.0f);
		if (timer_ >= clearMessageSeconds_) {
			// リザルト画面が用意されていないシーンでは、従来どおりそのままタイトルへ戻る。
			if (resultMenuName_.empty()) {
				ReturnToTitle();
				break;
			}
			// 死亡時と同じ流儀で、暗転しきってから差し替える。
			if (ScreenFader* fader = ScreenFader::FindInScene(owner_ ? owner_->GetScene() : nullptr)) {
				fader->FadeOut(deathFadeOutSeconds_);
			}
			state_ = State::ClearFadeOut;
			timer_ = 0.0f;
		}
		break;

	case State::ClearFadeOut: {
		timer_ += deltaTime;
		ScreenFader* fader = ScreenFader::FindInScene(owner_ ? owner_->GetScene() : nullptr);
		const bool covered = !fader || fader->IsOpaque() || timer_ >= deathFadeOutSeconds_;
		if (!covered) {
			break;
		}
		SetObjectActive(clearMessageName_, false);
		FillResultTexts();
		SetObjectActive(resultMenuName_, true);
		if (fader) {
			fader->FadeIn(deathFadeInSeconds_);
		}
		state_ = State::ResultMenu;
		timer_ = 0.0f;
		break;
	}

	case State::ResultMenu:
		// リトライメニューと同じ理由で毎フレーム止め直す。
		Time::SetTimeScale(0.0f);
		if (autoRetry_) {
			timer_ += deltaTime;
			if (timer_ >= autoRetryDelay_) {
				Retry();
			}
		}
		break;

	case State::PhaseTransition:
		// 繋ぎの演出はPhase2Cutsceneが自分で進める。こちらは勝敗判定を止めておくだけ。
		break;

	case State::Leaving:
		break;
	}
}

void GameFlowManager::RecordOutcome(bool won) {
	int& runs = GameSession::AutoBattleRunsRef();
	int& wins = GameSession::AutoBattleWinsRef();
	++runs;
	if (won) {
		++wins;
	}

	// **1行に必要な全部を書く。** 後からログを眺めるだけで勝率が読めるようにしておかないと、
	// 20連戦を回した意味が薄れる。
	float rate = (runs > 0) ? (100.0f * static_cast<float>(wins) / static_cast<float>(runs)) : 0.0f;

	// **判定の根拠になった数字も一緒に残す。** 勝敗だけだと、
	// 「本当にボスを削り切ったのか」「どちらが先に倒れたのか」が後から確かめられない。
	float bossPercent = bossHealth_ ? bossHealth_->GetHealthPercent() : -1.0f;
	float leaderPercent = -1.0f;
	float allyPercent = -1.0f;
	if (PartyManager* party = PartyManager::FindInScene(owner_ ? owner_->GetScene() : nullptr)) {
		if (GameObject* leader = party->GetLeader()) {
			if (PlayerHealth* health = leader->GetComponent<PlayerHealth>()) {
				leaderPercent = health->GetHealthPercent();
			}
		}
		if (GameObject* ally = party->GetAlly()) {
			if (PlayerHealth* health = ally->GetComponent<PlayerHealth>()) {
				allyPercent = health->GetHealthPercent();
			}
		}
	}

	// **どのシーンでの決着かも残す。**
	// 形態が分かれている今、勝敗だけでは「第1形態で全滅」と「第2形態で全滅」を
	// 後から区別できず、どちらを調整すべきかがログから読めない。
	const char* sceneName = (owner_ && owner_->GetScene()) ? owner_->GetScene()->GetSceneName() : "?";

	char buffer[256];
	std::snprintf(buffer, sizeof(buffer), "[Battle] %s  %-16s time=%.1fs  boss=%.0f%%  party=%.0f%%/%.0f%%  %d/%d  winRate=%.1f%%",
	    won ? "WIN " : "LOSE", sceneName, battleSeconds_, bossPercent * 100.0f, leaderPercent * 100.0f, allyPercent * 100.0f, wins,
	    runs, rate);
	Logger::Log(buffer);

	// **自動計測中はファイルにも残す。** Logger::Log は _DEBUG でしか出ないのに対し、
	// 自動でPlayに入るのはReleaseビルドなので、そのままでは肝心の勝率が読めない。
	// Auto Retry を明示的にONにしたときだけ書くので、通常プレイでは1バイトも増えない。
	if (autoRetry_) {
		std::filesystem::path logPath = GetProjectDataRoot() / "battle_result.log";
		std::FILE* file = nullptr;
		if (fopen_s(&file, logPath.string().c_str(), "a") == 0 && file) {
			std::fprintf(file, "%s\n", buffer);
			std::fclose(file);
		}
	}
}

void GameFlowManager::Retry() {
	if (state_ == State::Leaving) {
		return; // 連打で二重に切り替えない。
	}
	Time::SetTimeScale(1.0f);
	++GameSession::RetryCountRef();

	std::string target = retrySceneName_;
	if (target.empty()) {
		Scene* scene = owner_ ? owner_->GetScene() : nullptr;
		if (scene) {
			target = scene->GetSceneName();
		}
	}
	if (target.empty()) {
		return;
	}
	state_ = State::Leaving;
	ScreenFader::RequestTransition(owner_ ? owner_->GetScene() : nullptr, target, true);
}

void GameFlowManager::ReturnToTitle() {
	if (state_ == State::Leaving) {
		return;
	}
	Time::SetTimeScale(1.0f);
	GameSession::ResetProgress();
	state_ = State::Leaving;
	ScreenFader::RequestTransition(owner_ ? owner_->GetScene() : nullptr, titleSceneName_, true);
}

bool GameFlowManager::IsBossDefeated() {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || bossName_.empty()) {
		return false;
	}
	if (!bossHealth_) {
		GameObject* bossObject = scene->FindGameObjectByName(bossName_);
		bossHealth_ = bossObject ? bossObject->GetComponent<EnemyHealth>() : nullptr;
	}
	// 見つかっていない間はクリアにしない(名前を間違えたときに即クリアするのを避ける)。
	return bossHealth_ && !bossHealth_->IsAlive();
}

bool GameFlowManager::IsPartyWiped() const {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	PartyManager* party = PartyManager::FindInScene(scene);
	if (!party) {
		return false;
	}

	int aliveCount = 0;
	int memberCount = 0;
	for (GameObject* member : {party->GetLeader(), party->GetAlly()}) {
		if (!member) {
			continue;
		}
		PlayerHealth* health = member->GetComponent<PlayerHealth>();
		if (!health) {
			continue;
		}
		++memberCount;
		if (!health->IsDead()) {
			++aliveCount;
		}
	}
	// 1人も把握できていないときは判定しない(Play開始直後の解決前など)。
	return memberCount > 0 && aliveCount == 0;
}

void GameFlowManager::CaptureFadeTargets(GameObject* root) {
	fadeTargets_.clear();
	if (!root) {
		return;
	}
	// 自分と子孫のImage/Textを集める。元の色を控えておき、αだけを演出で動かす。
	std::vector<GameObject*> stack{root};
	while (!stack.empty()) {
		GameObject* node = stack.back();
		stack.pop_back();
		if (!node) {
			continue;
		}
		if (ImageComponent* image = node->GetComponent<ImageComponent>()) {
			fadeTargets_.push_back({image, nullptr, image->GetColor()});
		}
		if (TextComponent* text = node->GetComponent<TextComponent>()) {
			fadeTargets_.push_back({nullptr, text, text->GetColor()});
		}
		for (GameObject* child : node->GetChildren()) {
			stack.push_back(child);
		}
	}
}

void GameFlowManager::ApplyFadeRatio(float ratio) {
	const float clamped = (ratio < 0.0f) ? 0.0f : ((ratio > 1.0f) ? 1.0f : ratio);
	for (const FadeTarget& target : fadeTargets_) {
		Vector4 color = target.baseColor;
		color.w = target.baseColor.w * clamped;
		if (target.image) {
			target.image->SetColor(color);
		} else if (target.text) {
			target.text->SetColor(color);
		}
	}
}

GameObject* GameFlowManager::SetObjectActive(const std::string& name, bool active) {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || name.empty()) {
		return nullptr;
	}
	GameObject* object = scene->FindGameObjectByName(name);
	if (object) {
		object->SetActive(active);
	}
	return object;
}
