#include "PartyManager.h"
#include "GameAudio.h"
#include "GameFx.h"
#include "AllyAIBrain.h"
#include "CharacterMotor.h"
#include "GameEvents.h"
#include "GameInput.h"
#include "GameSession.h"
#include "PartySelection.h"
#include "Player.h"
#include "PlayerHealth.h"
#include "ThreatBoard.h"

#include <components/OrbitCameraComponent.h>
#include <utility>

using namespace KujataEngine;

void PartyManager::OnPlayStart() {
	leader_ = nullptr;
	ally_ = nullptr;
	swapCooldownTimer_ = 0.0f;

	// **攻撃予告板はここで捨てる。** コンポーネントは使い回されるので、
	// 前回Playの掲示が残っていると、開始直後に存在しない攻撃から逃げ始める。
	Threat::Clear();

	if (!owner_ || !owner_->GetScene()) {
		return;
	}

	// キャラ選択シーンからの持ち込みがあれば、Inspector設定より優先する。
	// 選ばれた方をリーダーへ、Inspector上のリーダーだった方を味方へ回す。
	std::string selected = PartySelection::ConsumeLeaderName();
	if (selected.empty()) {
		// PartySelectionは1回で消費されるため、シーンを跨いだ2回目以降はこちらから拾う
		// (これが無いとチュートリアル→ボス戦やリトライで選択がInspectorの既定へ戻ってしまう)。
		selected = GameSession::LeaderNameRef();
	}
	if (!selected.empty() && selected != leaderName_) {
		if (selected == allyName_) {
			allyName_ = leaderName_;
		}
		leaderName_ = selected;
	}

	Scene* scene = owner_->GetScene();
	leader_ = scene->FindGameObjectByName(leaderName_);
	ally_ = scene->FindGameObjectByName(allyName_);

	ApplyRoles();
}

void PartyManager::Update() {
	// **攻撃予告板の時計はここだけで進める。** 毎フレーム1回であることが前提の作りなので、
	// シーンに1つしか無いこのコンポーネントが受け持つ。
	// スケール済みの時間を使うのは、攻撃側のフェーズ管理と同じ土俵に乗せるため
	// (ヒットストップで世界が止まれば、命中予定時刻も一緒に止まる)。
	Threat::Advance(Time::GetDeltaTime());

	if (swapCooldownTimer_ > 0.0f) {
		swapCooldownTimer_ -= Time::GetDeltaTime();
		if (swapCooldownTimer_ < 0.0f) {
			swapCooldownTimer_ = 0.0f;
		}
	}

	// 操作キャラが倒れたら、生きている相方へ自動で乗り移る。
	// **これが無いと、リーダーが倒れた時点で操作先が死体のままになり、
	// 自動蘇生([[death-and-revive]])を待つ十秒間ずっと何も動かせなくなる。**
	SwapIfLeaderIsDown();

	// 自動戦闘中はプレイヤーが居ない前提なので、切替入力も受け付けない。
	if (swapEnabled_ && !autoBattle_ && GameInput::IsSwapCharacterTriggered()) {
		SwapLeader();
	}

}

bool PartyManager::SwapLeader() {
	// **通らなかったときは理由を掲示する。** 切替は失敗しても画面が何も変わらないので、
	// 「押したのに反応しない」と「そもそも押せていない」の区別が付かなくなる([[GameEvents]])。
	if (!leader_ || !ally_ || swapCooldownTimer_ > 0.0f) {
		GameEvents::ReportFailure(GameEvents::Failure::SwapFailed);
		return false;
	}

	// 相方が倒れていたら切り替えられない。
	if (PlayerHealth* allyHealth = ally_->GetComponent<PlayerHealth>()) {
		if (!allyHealth->IsAlive()) {
			GameEvents::ReportFailure(GameEvents::Failure::SwapFailed);
			return false;
		}
	}
	// どちらかが被弾硬直中なら切り替えられない(硬直を切替で逃げられないように)。
	for (GameObject* character : {leader_, ally_}) {
		if (CharacterMotor* motor = character->GetComponent<CharacterMotor>()) {
			if (motor->IsStunned()) {
				GameEvents::ReportFailure(GameEvents::Failure::SwapFailed);
				return false;
			}
		}
	}

	PerformSwap();
	swapCooldownTimer_ = swapCooldown_;
	++GameEvents::SwapCountRef();
	// **通ったときだけ鳴らす。** 失敗の合図は [[ActionFeedback]] が別の音で出すので、
	// ここで両方鳴らすと成功と失敗が同じ手応えになってしまう。
	GameAudio::PlaySe(GameAudio::Se::CharacterSwitch);
	return true;
}

void PartyManager::SwapIfLeaderIsDown() {
	if (!leader_ || !ally_) {
		return;
	}
	PlayerHealth* leaderHealth = leader_->GetComponent<PlayerHealth>();
	PlayerHealth* allyHealth = ally_->GetComponent<PlayerHealth>();
	if (!leaderHealth || !allyHealth) {
		return;
	}
	if (!leaderHealth->IsDead() || allyHealth->IsDead()) {
		return;
	}
	// 倒れた側からの移乗なので、SwapLeaderの可否判定(相方の生死・硬直・クールダウン)は通さない。
	PerformSwap();
	swapCooldownTimer_ = swapCooldown_;
}

void PartyManager::PerformSwap() {
	// これまで味方として動いていたAI頭脳(=これからリーダーになる側)は、監視オブザーバーと
	// 実行中の分岐を手放す。手放さないと、新しく味方になる側がオブザーバーを作れず
	// BahamutAIEditorへ実行フローが届かない(同一キーは最初の登録者だけが送信する)。
	if (AllyAIBrain* allyBrain = ally_->GetComponent<AllyAIBrain>()) {
		allyBrain->OnRelievedFromDuty();
	}

	std::swap(leader_, ally_);
	std::swap(leaderName_, allyName_);

	// **入れ替わる2人の足元それぞれから魂の炎が立つ。** 「魂が移った」ことを2点で示す。
	// 色は魂の色で上書きするので、Prefab側の色を変えても揃ったままになる。
	for (GameObject* character : {leader_, ally_}) {
		if (character) {
			GameFx::Burst(character->GetScene(), GameFx::Prefab::kSoulBurst, character->GetTransform().translation_, 1.0f,
			    &GameFx::kSoulColor);
		}
	}
	ApplyRoles();
}

PartyManager* PartyManager::FindInScene(Scene* scene) {
	if (!scene) {
		return nullptr;
	}
	for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (PartyManager* manager = object->GetComponent<PartyManager>()) {
			return manager;
		}
	}
	return nullptr;
}

GameObject* PartyManager::FindLeaderInScene(Scene* scene, const std::string& fallbackName) {
	if (!scene) {
		return nullptr;
	}
	if (PartyManager* manager = FindInScene(scene)) {
		if (manager->GetLeader()) {
			return manager->GetLeader();
		}
	}
	return fallbackName.empty() ? nullptr : scene->FindGameObjectByName(fallbackName);
}

void PartyManager::ApplyRoles() {
	SetBrainMode(leader_, true);
	SetBrainMode(ally_, false);

	// カメラの追従先をリーダーへ向ける(OrbitCameraComponentは毎フレーム名前解決するため即時反映される)。
	if (owner_ && owner_->GetScene()) {
		if (GameObject* cameraObject = owner_->GetScene()->FindGameObjectByName(cameraName_)) {
			if (OrbitCameraComponent* orbitCamera = cameraObject->GetComponent<OrbitCameraComponent>()) {
				orbitCamera->SetTargetName(leaderName_);
			}
		}
	}
}

void PartyManager::SetBrainMode(GameObject* character, bool isLeader) {
	if (!character) {
		return;
	}

	// **自動戦闘では2人ともAI。** リーダーという役はカメラとUIの追従先として残るが、
	// 入力頭脳は両方切る(勝率の計測はここが「人の手が入っていない」ことの担保になる)。
	if (autoBattle_) {
		isLeader = false;
	}

	// 入力頭脳とAI頭脳を排他で切り替える。付いていない頭脳は無視する
	// (例: AllyAIBrain未設定のキャラをリーダーにしても問題なく動く)。
	if (Player* inputBrain = character->GetComponent<Player>()) {
		inputBrain->SetEnabled(isLeader);
	}
	if (AllyAIBrain* aiBrain = character->GetComponent<AllyAIBrain>()) {
		aiBrain->SetEnabled(!isLeader);
	}
}
