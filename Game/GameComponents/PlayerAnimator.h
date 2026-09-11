#pragma once
#include <KujataEngine.h>
#include <components/AnimatorComponent.h>

class CharacterMotor;

/// <summary>
/// コントローラーのR2(右トリガー)でAnimatorのアニメーションを再生するゲームComponent。
/// 同じGameObjectのAnimatorComponentを使う。Clip Nameを指定するとそのクリップへ切り替えて再生し、
/// 空なら選択中クリップを再生する。硬直中(被弾のけぞり中)は入力を受け付けない。
/// </summary>
class PlayerAnimator : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "PlayerAnimator"; }
	bool AllowMultiple() const override { return false; }

	void Update() override;
	void OnPlayStart() override;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED(clipName_, "Clip Name");
		KUJATA_REGISTER_FLOAT_NAMED(triggerThreshold_, "Trigger Threshold", 0.01f, 0.05f, 1.0f);
		KUJATA_REGISTER_BOOL_NAMED(stopOnRelease_, "Stop On Release");
	}

	// 再生するクリップ名(クリップJSONのname)。空なら選択中クリップを再生する。
	KUJATA_FIELD_STRING(clipName_, "");
	// この値以上トリガーを引いたら「押した」とみなす。
	KUJATA_FIELD_FLOAT(triggerThreshold_, 0.5f);
	// 有効ならR2を離した時に再生を停止する。
	KUJATA_FIELD_BOOL(stopOnRelease_, false);

	KujataEngine::AnimatorComponent* animator_ = nullptr;
	CharacterMotor* motor_ = nullptr;

	bool wasPressed_ = false;
};
