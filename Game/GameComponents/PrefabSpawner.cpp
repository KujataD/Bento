#include "PrefabSpawner.h"

#include <Editor/PrefabAsset.h>

using namespace KujataEngine;

void PrefabSpawner::OnPlayStart() {
	// 前回Playのポインタは無効。**必ず捨ててから作り直す。**
	spawned_ = nullptr;
	// **生成はここではやらない。** 理由はUpdate側のコメントを参照。
	pending_ = true;
}

void PrefabSpawner::OnPlayStop() {
	spawned_ = nullptr;
	pending_ = false;
}

void PrefabSpawner::Update() {
	if (!pending_) {
		return;
	}
	pending_ = false;

	// **生成は OnPlayStart ではなく最初の Update で行う。**
	//
	// PrefabAsset::Instantiate はシーンの GameObject 配列へ push_back する。
	// `Scene::Update` はそれを見越して**添字で**回しているが、`Scene::OnPlayStart` は範囲forなので、
	// そこで生成すると配列の再確保でイテレータが無効になり、以降の OnPlayStart が
	// 解放済みメモリを触って落ちる。**1フレーム遅らせるだけで安全な側へ移せる。**
	if (!owner_ || !owner_->GetScene() || prefabPath_.empty()) {
		return;
	}

	PrefabAsset::InstantiateResult result = PrefabAsset::Instantiate(*owner_->GetScene(), prefabPath_, false);
	if (!result.succeeded || !result.rootObject) {
		Logger::Log("[PrefabSpawner] prefab load failed (" + prefabPath_ + "): " + result.message);
		return;
	}
	spawned_ = result.rootObject;

	if (!overrideName_.empty()) {
		spawned_->SetName(overrideName_);
	}
	if (useOwnerTransform_) {
		const WorldTransform& source = owner_->GetTransform();
		WorldTransform& destination = spawned_->GetTransform();
		destination.translation_ = source.translation_;
		destination.rotation_ = source.rotation_;
		destination.scale_ = source.scale_;
	}

	// **生成物へ Initialize と OnPlayStart を自分で通す。**
	//
	// Scene::AddGameObject は「まだ空のGameObject」に対して Initialize を呼ぶ作りで、
	// Prefabがコンポーネントを載せるのはその後。つまり生成物のコンポーネントには
	// **どちらのイベントも届かない**。衝撃刃や弾のように Transform とコライダーしか
	// 使わない物は困らないが、ボスのように「Initializeでノードを登録し、
	// OnPlayStartでBTを読む」種類のものは、これが無いと**一切動かないまま立っている**。
	BeginPlayRecursive(spawned_);
}

void PrefabSpawner::BeginPlayRecursive(GameObject* object) {
	if (!object) {
		return;
	}
	object->Initialize();
	object->OnPlayStart();
	// Prefabの子は同じシーンの平坦なリストにも入るが、イベントは自分で辿って配る。
	for (GameObject* child : object->GetChildren()) {
		BeginPlayRecursive(child);
	}
}
