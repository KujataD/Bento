#pragma once
#include <KujataEngine.h>
#include <components/ParticleSystemComponent.h>

#include <string>
#include <vector>

/// <summary>
/// ゲーム共通のFX定義と、ワンショットFXを鳴らすための小さな窓口。
///
/// **FXごとに子オブジェクトを生やさない。** 土埃やヒットは「その瞬間・その場所」で出したいだけで、
/// 発生源に付いて回る必要がない。発生源へ子を足していくと、Prefabが増えるたびに
/// 同じ子を付け忘れる事故が起きるので、Prefabを1つ置いてプールから使い回す形にした。
/// </summary>
namespace GameFx {

/// <summary>
/// **魂の色。** プレイヤーの魂の炎とガーディアンのエンジンの炎が共有する。
/// 「ガーディアンは同じ魂を動力にしている」という設定を色で示すためのもので、
/// ここ1箇所を変えれば両方に効く。加算合成で光らせる前提なので、少し明るめに置いてある。
/// </summary>
inline constexpr KujataEngine::Vector4 kSoulColor = {0.35f, 1.0f, 0.55f, 1.0f};

/// <summary>FXのPrefabパス。増やすときはここへ足す。</summary>
namespace Prefab {
inline const std::string kDust = "Prefabs/FxDust.prefab.json";
inline const std::string kMagicHit = "Prefabs/FxMagicHit.prefab.json";
inline const std::string kSoulBurst = "Prefabs/FxSoulBurst.prefab.json";
inline const std::string kSoulPyre = "Prefabs/FxSoulPyre.prefab.json";
} // namespace Prefab

/// <summary>
/// 指定位置でワンショットFXを鳴らす。
///
/// strengthは粒の数・初速・大きさへまとめて掛かる倍率。
/// **同じPrefabで「軽い接地」と「渾身の踏みつけ」を出し分けるためのもの**で、
/// 土埃のPrefabを種類ぶん用意しなくて済むようにしてある。
///
/// colorを渡すと粒の色を上書きする(魂の色を流し込む用)。
/// </summary>
KujataEngine::GameObject* Burst(
    KujataEngine::Scene* scene, const std::string& prefabPath, const KujataEngine::Vector3& position, float strength = 1.0f,
    const KujataEngine::Vector4* color = nullptr);

/// <summary>
/// 指定位置に「出しっぱなしの炎」を置く。消えないので、置いた側が止める責任を持つ。
/// 死亡地点の炎のように、その場に残り続けてほしいものに使う。
/// </summary>
KujataEngine::GameObject* Ignite(
    KujataEngine::Scene* scene, const std::string& prefabPath, const KujataEngine::Vector3& position,
    const KujataEngine::Vector4* color = nullptr);

/// <summary>Igniteで置いた炎を止める(粒が消え切るまでは残る)。</summary>
void Extinguish(KujataEngine::GameObject* fxObject);

/// <summary>Play開始時にプールを捨てる。前回Playのポインタは無効なので必ず呼ぶこと。</summary>
void ResetPools();

} // namespace GameFx
