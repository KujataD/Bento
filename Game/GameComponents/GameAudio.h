#pragma once
#include <string>

/// <summary>
/// ゲーム側から音を鳴らす唯一の窓口。AudioManager(エンジン)を直接叩かず必ずここを通す。
///
/// **音源は `Data/Audio/` のmp3**(出典とライセンスは `Data/Audio/AudioSources.md`)。
/// 差し替えは GameAudio.cpp の表を1行書き換えるだけで済み、**呼び出し側は一切触らなくてよい。**
/// mp3はエンジン側(`AudioManager::LoadAudio`)が読み込み時にPCMへ展開する。
///
/// 音量は [[GameSettings]] を毎回参照するので、設定画面の変更が次の1発から効く。
/// BGMだけは鳴りっぱなしなので、RefreshVolumes()で再生中のボイスへ反映する。
/// </summary>
namespace GameAudio {

/// <summary>
/// 効果音の種類。**本番音源へ差し替えるときの単位**でもあるので、
/// 「同じ音でいいもの」はまとめ、「別音にしたいもの」は分けてある。
/// </summary>
enum class Se {
	PlayerSwing,  // 剣を振る(空振り含む)
	PlayerHit,    // こちらの攻撃が敵に当たった
	Guard,        // ガードで受けた
	JustGuard,    // ジャストガード成立(**専用音にする価値が一番高い**)
	GuardBreak,   // ガードを崩された
	Dodge,        // 回避のステップ
	PlayerDamage, // 被弾
	Critical,     // 致命の一撃
	MagicShot,    // 魔法弾の発射
	BossSlam,     // ボスの叩きつけ/衝撃波
	EnemyDown,    // 敵が倒れた
	UiMove,       // メニューのカーソル移動
	UiDecide,     // メニュー決定
	UiCancel,     // メニュー取り消し
	Death,        // 全滅

	// --- 操作の手応えを返すためのもの ---
	CharacterSwitch, // キャラ切替が通った
	StaminaEmpty,    // スタミナ切れで技が出せなかった
	LockOn,          // Z注目で対象を掴んだ
	LockOff,         // Z注目を外した
	BarrierHit,      // 術師のバリアが魔法を受け止めた
	ReviveStart,     // 倒れた(自動蘇生のカウント開始)
	ReviveComplete,  // 自力で起き上がった

	// --- ボスの大技 ---
	Phase2Transition, // 第2形態への移行
	Count,
};

/// <summary>
/// 使う効果音を先に全部読み込んでおく。**戦闘中の初回ヒットでディスクを叩かせない**ため、
/// シーン開始時に一度呼ぶ([[BgmPlayer]]が呼んでいる)。読み込み済みなら何もしない。
/// </summary>
void PreloadSe();

/// <summary>効果音を1発鳴らす。多重再生されるので連打しても潰れない。</summary>
void PlaySe(Se se);

/// <summary>
/// BGMをループ再生する(既に同じ曲が鳴っていれば何もしない)。
/// パスはプロジェクトのData相対(例: "Audio/bg_Title.mp3")。mp3もwavも読める。
/// </summary>
void PlayBgm(const std::string& relativePath);

/// <summary>
/// ボス撃破のBGM。**一発の効果音ではなく曲**として鳴らす —
/// 撃破からリザルト画面まで同じシーンで続くので、鳴らしっぱなしにしたいのはこちら。
/// </summary>
inline constexpr const char* kClearBgmPath = "Audio/bg_Clear.mp3";

/// <summary>BGMを止める。</summary>
void StopBgm();

/// <summary>設定の音量を再生中のBGMへ反映する(設定画面から呼ぶ)。</summary>
void RefreshVolumes();

/// <summary>
/// シーン切り替え・Play停止時の後始末。**鳴りっぱなしのBGMを必ず止める**
/// (止めないとタイトルへ戻ったときにボス戦の曲が重なる)。
/// </summary>
void Shutdown();

} // namespace GameAudio
