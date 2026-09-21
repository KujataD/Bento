#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace KujataEngine {

class AnimatorComponent;

/// <summary>
/// アニメーションクリップの編集(キーの追加・削除)。
/// Animation ウィンドウと CUI の animation.* コマンドは、どちらもここを呼ぶ(同じ処理)。
/// 変更はクリップのメモリ上にだけ入る。ファイルへは AnimatorComponent::SaveClip で保存する。
/// クリップはシーンとは別のファイルなので、シーンの Undo では戻らない。
/// </summary>
namespace AnimationEditing {

/// <summary>
/// 新しいクリップを <プロジェクト>/Data/Animations/<名前>.anim.json に作り、animator に持たせる。
/// 同じ名前のファイルがあると失敗する(上書きしない)。
/// </summary>
bool CreateClip(AnimatorComponent& animator, const std::string& clipName, std::filesystem::path& outPath, std::string& message);

/// <summary>
/// trackPath のトラック(無ければ作る)の time の位置にキーを打つ。同じ時刻のキーは値を上書きする。
/// currentValue はそのチャンネルの今の値(無ければ nullptr。カーブの今の値を使う)。
/// 加算トラックでは、Rec 開始時の基準値からの差分として記録する。前後のキーの接線を滑らかに整える。
/// 戻り値は追加したキーの番号。
/// </summary>
int AddKey(AnimatorComponent& animator, const std::string& trackPath, float time, const float* currentValue);

/// <summary>trackPath のトラックの、time から tolerance 以内にあるキーを消す。消せたらtrue。</summary>
bool RemoveKey(AnimatorComponent& animator, const std::string& trackPath, float time, float tolerance);

/// <summary>
/// キーを打てるチャンネル(トラックのパス)の一覧。自分と子孫のコンポーネントのアニメーション可能なフィールド。
/// 例: "RotatorComponent/speed"、子なら "Arm:Transform/rotation.z"。
/// </summary>
std::vector<std::string> ListChannels(AnimatorComponent& animator);

/// <summary>trackPath のチャンネルの今の値を探す(見つからなければ nullptr)。</summary>
float* FindChannelValue(AnimatorComponent& animator, const std::string& trackPath);

} // namespace AnimationEditing

} // namespace KujataEngine
