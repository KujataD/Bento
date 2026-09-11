# Audio source manifest

このフォルダーの音源は配布元から取得した未加工MP3です。
命名は背景音を `bg_`、効果音を `se_` で統一しています。

KujataEngine の `AudioManager` はWAVのみを読み込むため、実装へ組み込む前に必要な尺・余韻・ループを調整し、PCM WAVへ変換してください。

## ライセンス

- 効果音: [効果音ラボ](https://soundeffect-lab.info/)
  - 使用報告・リンク・クレジット表記は不要です。
  - ゲーム内利用と加工は可能です。素材単体または加工素材の再配布は禁止です。
- BGM: [Springin' Sound Stock](https://www.springin.org/sound-stock/)
  - 使用報告・クレジット表記は不要です。
  - ゲーム内利用と加工は可能です。素材単体または加工素材の再配布は禁止です。

## 背景音

| ファイル | 原素材 | 用途 |
|---|---|---|
| `bg_BossPhase1.mp3` | Springin'「RPGバトル1」 | ボス第一形態 |
| `bg_BossPhase2.mp3` | Springin'「RPGバトル2」 | ボス第二形態 |
| `bg_Title.mp3` | Springin'「キネマティック4」 | タイトル |
| `bg_CharacterSelect.mp3` | Springin'「キネマティック4」 | キャラクター選択 |
| `bg_Result.mp3` | Springin'「キネマティック3」 | 撃破後。オルゴール不使用 |
| `bg_Tutorial.mp3` | 効果音ラボ「水のしたたる洞窟」 | チュートリアル環境音 |
| `bg_Tutorial_WindLayer.mp3` | 効果音ラボ「風が吹く3」 | チュートリアル用追加レイヤー |

敗北画面とローディング画面の背景音は設計どおり追加していません。

## 基本効果音

| ファイル | 原素材 |
|---|---|
| `se_JustGuard.mp3` | 剣で打ち合う2 |
| `se_PlayerHit.mp3` | ロボットを殴る1 |
| `se_BossSlam.mp3` | 打撃4 |
| `se_BossSlam_RumbleLayer.mp3` | 地響き |
| `se_PlayerDamage.mp3` | 重いパンチ1 |
| `se_Guard.mp3` | 盾で防御 |
| `se_GuardBreak.mp3` | 石が砕ける |
| `se_Dodge.mp3` | キックの衣擦れ2 |
| `se_Critical.mp3` | 斧で斬る2 |
| `se_PlayerSwing.mp3` | 剣の素振り2 |
| `se_MagicShot.mp3` | 風魔法2 |
| `se_EnemyDown.mp3` | 倒れる |
| `se_Death.mp3` | 荘厳な雰囲気 |
| `se_UiDecide.mp3` | 決定ボタンを押す14 |
| `se_UiCancel.mp3` | キャンセル8 |
| `se_UiMove.mp3` | カーソル移動4 |

`Clear` は既存の `Data/Resources/fanfare.wav` を継続使用します。

## 追加効果音

| ファイル | 原素材 |
|---|---|
| `se_BossChargeStomp.mp3` | パワーチャージ |
| `se_BossChargeSweep.mp3` | 武器をクルクル回す |
| `se_BeamCharge.mp3` | ビーム砲チャージ |
| `se_BeamLoop.mp3` | 殺人音波 |
| `se_BossFootstep.mp3` | 怪獣の足音 |
| `se_ReviveStart.mp3` | 魔法陣を展開 |
| `se_ReviveComplete.mp3` | 回復魔法4 |
| `se_BarrierDeploy.mp3` | 魔法反射 |
| `se_BarrierHit.mp3` | ロボットを強く殴る2 |
| `se_BarrierBreak.mp3` | ガラスが割れる1 |
| `se_CharacterSwitch.mp3` | 決定ボタンを押す19 |
| `se_StaminaEmpty.mp3` | 心臓の鼓動2 |
| `se_LockOn.mp3` | 決定ボタンを押す9 |
| `se_LockOff.mp3` | キャンセル8 |
| `se_StarImpact.mp3` | 岩が真っ二つに割れる |
| `se_Collapse.mp3` | 建物が大きく崩れる1 |

## 第二形態移行用の未加工レイヤー

| ファイル | 原素材 |
|---|---|
| `se_Phase2Transition.mp3` | 建物が大きく崩れる1 |
| `se_Phase2Transition_FearLayer.mp3` | 迫り来る恐怖 |
| `se_Phase2Transition_FlightLayer.mp3` | 魔法使いが空を飛ぶ |
| `se_Phase2Transition_RumbleLayer.mp3` | 地響き |

第二形態移行用の素材にはビーム音を含めていません。
