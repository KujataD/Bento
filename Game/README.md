# BOARD:BORDER(ボス戦アクション)

このリポジトリ(CG3)のゲーム。パーティで巨大なガーディアンに挑むボス戦アクション。
課題の夏提出版はコミット `e7ce911`(エンジンとゲームの分離より前)。

## 必要なもの

- このリポジトリの隣に [BahamutAIMiddleware](https://github.com/KujataD/BahamutAIMiddleware) を置く(敵・味方AIのビヘイビアツリーに使う)
  - そのため、このリポジトリの `KujataEngine.sln` には BahamutAICore プロジェクトが入っている(エンジン用リポジトリには入っていない)

## このゲーム固有の設定の置き場所

| 設定 | 場所 |
|---|---|
| exe 名(課題の提出名) | `Game/Game.props` の `KujataExeName` |
| ウィンドウのタイトル・背景色 | `Game/Data/ProjectSettings/Project.json` |
| 起動シーン | `Game/Data/ProjectSettings/StartupScene.txt`(配布物は `Tools/MakeGameBuild.ps1` が TitleScene にする) |
| タグ一覧 | `Game/Data/ProjectSettings/Tags.json` |
| AI のビヘイビアツリー | `Game/Data/Resources/bt_set/` |

背景色がほぼ黒なのは、画面奥を洞窟の闇に見せるため(空色のままだと、天井の無い通路から明るい「空」が覗いてしまう)。
