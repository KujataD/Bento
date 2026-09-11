# KujataEngine

DirectX 12 製の自作ゲームエンジン(Unity 風エディタ内蔵)。
エンジンは `KujataEngine/`、外部ライブラリは `externals/`、ゲームは `DirectXGame/` にフォルダで分けてあり、**1リポジトリ=1ゲーム**で運用する。
このリポジトリのゲームについては [DirectXGame/README.md](../DirectXGame/README.md) を参照。

## 必要環境

- Windows 11 / Visual Studio 18(2026、ツールセット v145、x64)
- ソリューション: `KujataEngine.sln`(exe = エンジン/エディタ、`GameModule` = ゲームロジック DLL)
- **Git LFS**(assimp のライブラリを LFS で管理している。Git for Windows に同梱。初めて使う PC では clone の前に一度 `git lfs install` を実行する)
- ゲームによっては追加で必要なもの(隣に置くライブラリ等)がある。`DirectXGame/README.md` を確認する

## ビルドと実行

1. `KujataEngine.sln` を Visual Studio 18 で開き、**Debug | x64** でビルド(exe と GameModule は必ず同じ .sln から同時にビルドすること)
2. 実行するとエディタが起動し、`DirectXGame/` のゲームが開く。Hierarchy でオブジェクト選択、▶ で Play / ■ で停止
   - 別のフォルダを開くときは、起動引数に `--project <フォルダ>` を付ける(例: 試作用の `Sandbox/`)
3. 遊んでもらう用の配布フォルダは、Release をビルドしてから `Tools/MakeGameBuild.ps1` で作る

### エディタ操作(Scene ウィンドウ)

- クリックでフォーカスしてから: WASD 移動 / QE 上下 / 右クリックホールド+マウスで視点

## 新しいゲームを作る

エンジン用リポジトリ KujataEngine を clone して作る(`DirectXGame/` は空のテンプレートになっている)。

```bash
git clone https://github.com/KujataD/KujataEngine MyGame
```

1. clone したフォルダで、元のリポジトリを `engine` という名前に変える: `git remote rename origin engine`
2. GitHub で空のリポジトリを作り、`origin` として登録して push する
3. `DirectXGame/Game.props` の `KujataExeName`(exe 名)と `DirectXGame/Data/ProjectSettings/Project.json`(ウィンドウタイトル)を書き換える
4. `DirectXGame/README.md` をそのゲームの説明に書き換える
5. ゲームのコードは `DirectXGame/GameComponents/` に書き、`DirectXGame/GameModule/GameModule.cpp` で登録する

**プロジェクトのパスに日本語を含めないこと**(テクスチャの読み込みが失敗する)。

## エンジンの更新を取り込む

```bash
git fetch engine
```

のあと `git merge engine/main` で取り込む。ゲームのリポジトリで `KujataEngine/` と `externals/` を変えていなければ、衝突はほぼ起きない。
ゲーム側でエンジンを直した場合は、そのコミットを KujataEngine へ cherry-pick で持ち帰る。

## フォルダ構成

| パス | 内容 |
|---|---|
| `KujataEngine/` | エンジン本体(scene / runtime / components / Editor / 3d / 2d / base / postprocess / shapes / math / vfx / shadow / assets / input、`KujataEngine.vcxproj`、`main.cpp`) |
| `KujataEngine/EngineData/` | エンジンが持つデータ(シェーダー、既定テクスチャ) |
| `externals/` | 外部ライブラリ(imgui / assimp / DirectXTex 等) |
| `DirectXGame/` | このリポジトリのゲーム(GameModule / GameComponents / Data / Game.props / README.md) |
| `Sandbox/` | 使い捨ての試作プロジェクト(git 管理外) |
| `Tools/` | 配布フォルダ作成などのスクリプト |
| `docs/` | 提出資料・設計ドキュメント |
| `build/` | ビルド生成物(git 管理外) |

## ドキュメントの運用方法

このリポジトリのドキュメントは 4 か所で運用する。

1. **.claude/ReadMe.md(本ファイル)** — 人間向けの入口。エンジン共通の概要・ビルド手順・操作方法だけを置く。
2. **[.claude/CLAUDE.md](CLAUDE.md)** — AI アシスタント(Claude Code)と共有する開発コンテキスト。規約・罠・現在の方針を記載し、方針転換や構成変更のたびにその場で更新する。AI とのセッション開始時に自動で読み込まれる。
3. **DirectXGame/README.md** — そのゲーム固有の説明・依存・設定の置き場所。
4. **docs/** — 詳細資料の置き場(提出資料、今後のコードリーディングで作る図解 `docs/architecture/` 等)。

1 と 2 は全ゲームのリポジトリで同じ内容に保つ(エンジン更新の取り込みで衝突させないため)。
原則: **コードや git log から分かることはドキュメントに書かない**(二重管理で腐るため)。書くのは「コードから読み取れない意図・規約・罠」だけ。
