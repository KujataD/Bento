#pragma once

#include "KujataApi.h"
#include <string>
#include <vector>

namespace KujataEngine {

/// <summary>
/// Inspectorで録画された1件の変更。pathは "ComponentTypeName/チャンネル名" 形式。
/// </summary>
struct AnimationRecordedChange {
	std::string path;
	float value = 0.0f;
};

// AnimationWindowの録画モード状態。PlayState同様、GameModule側のComponent(Inspector描画)からも
// 見えるようランタイム層が所有し、Editorが設定する。

/// <summary>録画中(赤丸)かどうか。録画中はInspectorの値変更が自動でキーになります。</summary>
KUJATA_API bool IsAnimationRecording();
KUJATA_API void SetAnimationRecording(bool recording);

/// <summary>
/// フィールド右クリックの"Add Keyframe"を出せる状態か(AnimationWindowが選択中Animatorのクリップを開いている)。
/// </summary>
KUJATA_API bool IsAnimationKeyContextEnabled();
KUJATA_API void SetAnimationKeyContextEnabled(bool enabled);

/// <summary>
/// いまInspectorを描画中のComponent型名を設定します(nullptrで記録対象外)。
/// InspectorWindowがComponentごとに設定し、チャンネルpathのprefixになります。
/// </summary>
KUJATA_API void SetAnimationRecordingComponentContext(const char* componentTypeName);
KUJATA_API const char* GetAnimationRecordingComponentContext();

/// <summary>
/// チャンネル変更を記録キューへ積みます(Component文脈が無い場合は無視)。
/// channelPathはComponent内のチャンネル名(例 "speed", "translation.x")。
/// </summary>
KUJATA_API void ReportAnimationChannelChange(const std::string& channelPath, float value);

/// <summary>
/// 積まれた変更を取り出してキューを空にします(AnimationWindowが毎フレーム回収)。
/// </summary>
KUJATA_API std::vector<AnimationRecordedChange> ConsumeAnimationRecordedChanges();

} // namespace KujataEngine
