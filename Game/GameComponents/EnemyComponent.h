#pragma once

#include <BahamutAI/AI.h>
#include <KujataEngine.h>

#include "Player.h"

class EnemyComponent : public KujataEngine::Component {
public:
	enum class Fase {
		MoveForward,
		RotateY,
		MoveBackward,
		Forward,
	};

public:
	const char* GetTypeName() const override { return "EnemyComponent"; }

	void Initialize() override;
	void OnPlayStart() override;
	void Update() override;

	// UnityのButton.onClick等から呼べるメソッドを公開する。
	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;

private:
	void LoadBTSet();

	BahamutAI::BTStatus MoveForward(BahamutAI::AIContext& context);
	BahamutAI::BTStatus RotateY(BahamutAI::AIContext& context);
	BahamutAI::BTStatus MoveBackward(BahamutAI::AIContext& context);
	BahamutAI::BTStatus RotateX(BahamutAI::AIContext& context);
	BahamutAI::BTStatus MoveToTarget(BahamutAI::AIContext& context);

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT(speed_, 0.001f, 0.0f, 0.0f);
		// Inspectorに「Target」ドロップ欄が出る。Playerを持つGameObjectをドラッグ&ドロップで代入する。
		KUJATA_REGISTER_COMPONENT_REF_NAMED(target_, "Target");
	}

	KUJATA_FIELD_FLOAT(speed_, 0.02f);

	// 追従対象(PlayerのComponent参照)。target_->速度 のように直接publicへアクセスできる。
	KUJATA_FIELD_COMPONENT_REF(Player, target_);

	// BT作成用
	BahamutAI::BehaviorTreeFactory btFactory_;

	// BT実行本体（毎フレームTickする）
	BahamutAI::BehaviorTreeRuntime btRuntime_;

	// ライブ監視オブザーバー。
	// ComponentFactory登録時の使い捨てプロトタイプ生成でprimaryを奪われないよう、
	// 実際にPlayするインスタンスだけがOnPlayStartで遅延生成する(値メンバにしない)。
	std::unique_ptr<BahamutAI::UdpTreeObserver> btObserver_;

	// このEnemy固有のBlackboard（AIContextに渡す）
	BahamutAI::Blackboard localBlackboard_;
};
