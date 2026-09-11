#include "EnemyHealth.h"
#include "GameAudio.h"
#include "GameFx.h"

#include "HateTable.h"
#include <algorithm>

using namespace KujataEngine;

namespace {

// 名前で子孫を探す(自分自身も対象)。
GameObject* FindDescendantByName(GameObject* object, const std::string& name) {
	if (!object) {
		return nullptr;
	}
	if (object->GetName() == name) {
		return object;
	}
	for (GameObject* child : object->GetChildren()) {
		if (GameObject* found = FindDescendantByName(child, name)) {
			return found;
		}
	}
	return nullptr;
}

} // namespace

Vector3 EnemyHealth::GetLockOnPoint() const {
	if (!owner_) {
		return lockOnOffset_;
	}

	// 基準にする部位。指定が無ければ自分自身。
	GameObject* anchor = owner_;
	if (!lockOnObjectName_.empty()) {
		if (GameObject* found = FindDescendantByName(owner_, lockOnObjectName_)) {
			anchor = found;
		}
	}

	// 子オブジェクトのtranslation_は親からの相対なので、必ずワールド行列から取る。
	// (matWorld_は全Update後に更新されるため1フレーム古いが、狙い点の用途では問題にならない)
	Vector3 base = (anchor == owner_) ? owner_->GetTransform().translation_ : anchor->GetTransform().GetWorldPosition();
	return base + lockOnOffset_;
}

EnemyHealth* EnemyHealth::Master() {
	if (sharedHealthOwnerName_.empty() || !owner_ || !owner_->GetScene()) {
		return this;
	}
	GameObject* master = owner_->GetScene()->FindGameObjectByName(sharedHealthOwnerName_);
	if (!master || master == owner_) {
		return this;
	}
	EnemyHealth* health = master->GetComponent<EnemyHealth>();
	// **本体側がさらに誰かを指していても辿らない。** 1段だけにしておけば輪にならない。
	return health ? health : this;
}

const EnemyHealth* EnemyHealth::Master() const { return const_cast<EnemyHealth*>(this)->Master(); }

void EnemyHealth::Initialize() {
	health_ = maxHealth_;
	poise_ = 0.0f;
	poiseDecayTimer_ = 0.0f;
	stunTimer_ = 0.0f;
}

void EnemyHealth::OnPlayStart() {
	poise_ = 0.0f;
	poiseDecayTimer_ = 0.0f;
	stunTimer_ = 0.0f;
	// Playインスタンスは使い回される。消えかけのまま次のPlayに入らないよう必ず戻す。
	dissolving_ = false;
	dissolveTimer_ = 0.0f;
}

void EnemyHealth::Update() {
	float deltaTime = Time::GetDeltaTime();

	// 倒れた後の消滅演出。ここで早期returnはしない(共有HPの部位も同じUpdateを通る)。
	UpdateDissolve();

	// スタン中: 時間だけ進める(蓄積は0のまま)。
	if (stunTimer_ > 0.0f) {
		stunTimer_ -= deltaTime;
		if (stunTimer_ <= 0.0f) {
			stunTimer_ = 0.0f;
			if (onStaggerEnd_) {
				onStaggerEnd_();
			}
		}
		return;
	}

	// 蓄積の減衰(最後のヒットから待ち時間経過後)。
	if (poise_ <= 0.0f) {
		return;
	}
	if (poiseDecayTimer_ > 0.0f) {
		poiseDecayTimer_ -= deltaTime;
		return;
	}
	poise_ = std::max(0.0f, poise_ - poiseDecayPerSecond_ * deltaTime);
}

void EnemyHealth::TakeDamage(float damage) {
	// **HPを共有しているなら本体へ流す。** 部位ごとにHPを持たせない(バーが1本で済む)。
	if (EnemyHealth* master = Master(); master != this) {
		master->TakeDamage(damage);
		return;
	}
	if (!IsAlive()) {
		return;
	}

	health_ = std::max(0.0f, health_ - damage);
	// **手応えの音はここ1箇所で賄う。** 剣も魔法も致命も最後はこの関数を通るので、
	// 攻撃手段を足すたびに鳴らし忘れる作りにしない。
	GameAudio::PlaySe(GameAudio::Se::PlayerHit);

	if (onHealthChanged_) {
		onHealthChanged_(health_);
	}

	if (health_ <= 0) {
		GameAudio::PlaySe(GameAudio::Se::EnemyDown);
		BeginDissolve();
		if (onDeath_) {
			onDeath_();
		}
	}
}

void EnemyHealth::BeginDissolve() {
	// **演出が別に用意されている相手では消さない。** 第1形態のボスは撃破ではなく
	// 次の形態への繋ぎへ渡すので、ここで消すと演出が空振りする。
	if (!dissolveOnDeath_ || dissolving_ || !owner_) {
		return;
	}
	dissolving_ = true;
	dissolveTimer_ = 0.0f;
	dissolveBaseScale_ = owner_->GetTransform().scale_;

	// 崩れた足元から土埃が上がる。**縮み始めと同時に出す**ことで、
	// 「消えた」ではなく「崩れて土に還った」に見える。
	Vector3 origin = owner_->GetTransform().translation_;
	origin.y += dustHeight_;
	GameFx::Burst(owner_->GetScene(), GameFx::Prefab::kDust, origin, dustScale_);
}

void EnemyHealth::UpdateDissolve() {
	if (!dissolving_ || !owner_) {
		return;
	}
	dissolveTimer_ += Time::GetDeltaTime();
	const float span = (std::max)(dissolveSeconds_, 0.01f);
	const float t = std::clamp(dissolveTimer_ / span, 0.0f, 1.0f);

	// 素直に縮める。**最後まで縮めきってから消す**ので、消える瞬間が目に付かない。
	const float shrink = 1.0f - t;
	owner_->GetTransform().scale_ = {
	    dissolveBaseScale_.x * shrink, dissolveBaseScale_.y * shrink, dissolveBaseScale_.z * shrink};

	if (t >= 1.0f) {
		// **スケールは戻してから伏せる。** コンポーネントは使い回されるので、
		// 潰れたまま伏せると次のPlayで見えない敵が立つ。
		owner_->GetTransform().scale_ = dissolveBaseScale_;
		owner_->SetActive(false);
		dissolving_ = false;
	}
}

void EnemyHealth::TakeDamage(float damage, float poiseDamage, KujataEngine::GameObject* attacker) {
	TakeDamage(damage);
	AddPoise(poiseDamage);

	// **ヘイトの入口はここ1箇所にまとめる。**
	// 各ダメージ源(武器・魔法弾・致命)がそれぞれヘイトを積む作りにすると、
	// 新しい攻撃手段を足すたびに積み忘れが起きる。ダメージが通った場所で一括して通知する。
	if (attacker) {
		if (HateTable* hate = GetComponent<HateTable>()) {
			hate->AddDamageHate(attacker, damage);
		}
	}

	// **被弾のリアクションもここ1箇所から通知する。** ヘイトと同じ理由で、
	// ダメージが通った場所にまとめておかないと、攻撃手段を足すたびに拾い漏れる。
	// 体力を共有している部位(第2形態の目)を殴られた分も、頭脳が乗っている
	// まとめ役へ届くように Master() 経由で呼ぶ。
	if (EnemyHealth* master = Master()) {
		if (master->onHit_ && master->IsAlive() && !master->IsStaggered()) {
			master->onHit_(attacker, damage);
		}
	}
}

void EnemyHealth::AddPoise(float poiseDamage) {
	// 体勢崩しもHPと同じ本体へ集める。部位を殴っても同じゲージが溜まる。
	if (EnemyHealth* master = Master(); master != this) {
		master->AddPoise(poiseDamage);
		return;
	}
	if (!IsAlive() || poiseDamage <= 0.0f || IsStaggered()) {
		return;
	}

	poise_ += poiseDamage;
	poiseDecayTimer_ = poiseDecayDelay_;

	if (poise_ < poiseMax_) {
		return;
	}

	// 閾値到達: ゲージを空にしてスタン開始。頭脳へ通知(モーション中断はそちらで)。
	poise_ = 0.0f;
	poiseDecayTimer_ = 0.0f;
	stunTimer_ = stunDuration_;
	if (stunTimer_ > 0.0f && onStagger_) {
		onStagger_();
	}
}

void EnemyHealth::Flinch() {
	if (!IsAlive() || IsStaggered()) {
		return;
	}
	if (onFlinch_) {
		onFlinch_();
	}
}

float EnemyHealth::GetHealthPercent() const {
	if (const EnemyHealth* master = Master(); master != this) {
		return master->GetHealthPercent();
	}
	if (maxHealth_ <= 0) {
		return 0.0f;
	}
	return health_ / maxHealth_;
}

bool EnemyHealth::IsAlive() const {
	if (const EnemyHealth* master = Master(); master != this) {
		return master->IsAlive();
	}
	return health_ > 0;
}

void EnemyHealth::SetOnHealthChanged(std::function<void(float)> cb) {
	onHealthChanged_ = cb;
}

void EnemyHealth::SetOnDeath(std::function<void()> cb) {
	onDeath_ = cb;
}
