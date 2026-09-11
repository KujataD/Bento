# CG3 — KujataEngine 製のボス戦アクション

DirectX 12 製の自作ゲームエンジン **KujataEngine**(Unity 風エディタ内蔵)と、それを使ったボス戦アクションゲーム。
エンジンは `DirectXGame/`、ゲームは `Game/` にフォルダで分けてある(1リポジトリ=1ゲーム)。

## 必要環境

- Windows 11 / Visual Studio 18(2026、ツールセット v145、x64)
- このリポジトリの隣に [BahamutAIMiddleware](https://github.com/KujataD/BahamutAIMiddleware) を置く(ゲームの BT が使う)
- ソリューション: `KujataEngine.sln`(exe = エンジン/エディタ、`GameModule` = ゲームロジック DLL)

## ビルドと実行

1. `KujataEngine.sln` を Visual Studio 18 で開き、**Debug | x64** でビルド(exe と GameModule は必ず同じ .sln から同時にビルドすること)
2. 実行するとエディタが起動し、`Game/` のゲームが開く。Hierarchy でオブジェクト選択、▶ で Play / ■ で停止
   - 別のフォルダを開くときは、起動引数に `--project <フォルダ>` を付ける(例: 試作用の `Sandbox/`)
3. 遊んでもらう用の配布フォルダは、Release をビルドしてから `Tools/MakeGameBuild.ps1` で作る

### エディタ操作(Scene ウィンドウ)

- クリックでフォーカスしてから: WASD 移動 / QE 上下 / 右クリックホールド+マウスで視点
- ゲーム操作の詳細はゲーム内チュートリアル・設定画面を参照

## フォルダ構成

| パス | 内容 |
|---|---|
| `DirectXGame/KujataEngine/` | エンジン本体(scene / runtime / components / Editor / 3d / 2d / base / postprocess / shapes / math / vfx / shadow / assets / input) |
| `DirectXGame/EngineData/` | エンジンが持つデータ(シェーダー、既定テクスチャ) |
| `DirectXGame/externals/` | 外部ライブラリ(imgui / assimp / DirectXTex 等) |
| `Game/` | このリポジトリのゲーム(GameModule / GameComponents / Data / Game.props) |
| `Sandbox/` | 使い捨ての試作プロジェクト(git 管理外) |
| `Tools/` | 配布フォルダ作成などのスクリプト |
| `docs/` | 提出資料・設計ドキュメント |
| `build/` | ビルド生成物(git 管理外) |

ゲーム固有の設定(exe 名・ウィンドウタイトル・起動シーン)の置き場所は [Game/GameModule/README.md](../Game/GameModule/README.md) を参照。

## ドキュメントの運用方法

このリポジトリのドキュメントは 3 層で運用する。

1. **ReadMe.md(本ファイル)** — 人間向けの入口。概要・ビルド手順・操作方法だけを置く。操作や手順が変わったら更新する。
2. **[CLAUDE.md](CLAUDE.md)** — AI アシスタント(Claude Code)と共有する開発コンテキスト。規約・罠・現在の方針を記載し、方針転換や構成変更のたびにその場で更新する。AI とのセッション開始時に自動で読み込まれる。
3. **docs/** — 詳細資料の置き場。過去の提出資料([CG3 夏提出資料](../docs/CG3_夏提出資料.md))や、今後のコードリーディングで作る図解(`docs/architecture/` 予定)をここに蓄積する。

原則: **コードや git log から分かることはドキュメントに書かない**(二重管理で腐るため)。書くのは「コードから読み取れない意図・規約・罠」だけ。
