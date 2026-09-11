# KujataEngine

DirectX 12 製の自作ゲームエンジン(Unity 風エディタ内蔵)と、それを使ったボス戦アクションゲーム。

## 必要環境

- Windows 11 / Visual Studio 18(2026、ツールセット v145、x64)
- ソリューション: `KujataEngine.sln`(exe = エンジン/エディタ、`GameModule` = ゲームロジック DLL)

## ビルドと実行

1. `KujataEngine.sln` を Visual Studio 18 で開き、**Debug | x64** でビルド(exe と GameModule は必ず同じ .sln から同時にビルドすること)
   - 別のプロジェクトフォルダを開くときは、起動引数に `--project <フォルダ>` を付ける
2. 実行するとエディタが起動。Hierarchy でオブジェクト選択、▶ で Play / ■ で停止
3. 配布用の単体 exe フォルダは `Tools/MakeExeFile.ps1` で作成(Debug 構成必須)

### エディタ操作(Scene ウィンドウ)

- クリックでフォーカスしてから: WASD 移動 / QE 上下 / 右クリックホールド+マウスで視点
- ゲーム操作の詳細はゲーム内チュートリアル・設定画面を参照

## フォルダ構成

| パス | 内容 |
|---|---|
| `DirectXGame/KujataEngine/` | エンジン本体(scene / runtime / components / Editor / 3d / 2d / base / postprocess / shapes / math / vfx / shadow / assets / input) |
| `DirectXGame/GameComponents/` | ゲーム側ロジック(戦闘・ボスAI・UI) |
| `DirectXGame/externals/` | 外部ライブラリ(imgui / assimp / DirectXTex 等) |
| `Tools/` | 配布フォルダ作成などのスクリプト |
| `docs/` | 提出資料・設計ドキュメント |
| `build/` `Temp/` | ビルド生成物(git 管理外) |

## ドキュメントの運用方法

このリポジトリのドキュメントは 3 層で運用する。

1. **ReadMe.md(本ファイル)** — 人間向けの入口。概要・ビルド手順・操作方法だけを置く。操作や手順が変わったら更新する。
2. **[CLAUDE.md](CLAUDE.md)** — AI アシスタント(Claude Code)と共有する開発コンテキスト。規約・罠・現在の方針を記載し、方針転換や構成変更のたびにその場で更新する。AI とのセッション開始時に自動で読み込まれる。
3. **docs/** — 詳細資料の置き場。過去の提出資料([CG3 夏提出資料](../docs/CG3_夏提出資料.md))や、今後のコードリーディングで作る図解(`docs/architecture/` 予定)をここに蓄積する。

原則: **コードや git log から分かることはドキュメントに書かない**(二重管理で腐るため)。書くのは「コードから読み取れない意図・規約・罠」だけ。
