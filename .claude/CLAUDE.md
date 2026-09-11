# CLAUDE.md — KujataEngine 開発コンテキスト

このファイルは AI アシスタント(Claude Code 等)にプロジェクトの前提を共有するためのもの。
人間向けの概要は [ReadMe.md](ReadMe.md) を参照。

## プロジェクト概要

- **KujataEngine**: DirectX 12 製の自作ゲームエンジン(Unity 風エディタ内蔵)。このリポジトリ(CG3)は、それを使ったボス戦アクションゲーム。
- 作者は学生(専攻: **ゲームAI**)。応答・コメント・ドキュメントは日本語で書くこと。
- **1リポジトリ=1ゲーム**。エンジンは `DirectXGame/`、ゲームは `Game/` にフォルダで分けてある。新しいゲームはエンジン用リポジトリ(KujataEngine)を git clone して作る。ハブ(プロジェクト管理アプリ)は作らない。
- 構成: `KujataEngine.sln` → exe(エンジン/エディタ、`DirectXGame/KujataEngine.vcxproj`)+ `GameModule` DLL(ゲームロジック、`Game/GameModule/`、ホットリロード対応)。

## ビルドと検証

```bash
"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" KujataEngine.sln -p:Configuration=Debug -p:Platform=x64 -m -v:m -nologo
```

- **ユニットテストは運用しない方針**(Tests プロジェクトは削除済み)。検証は「ビルド成功+実機起動」で行う。
- **Component 等の共有ヘッダ(ABI)を変更したら、必ず .sln 経由で exe と GameModule を同時に再ビルド**すること。片方だけ古いと起動時にエントリポイントエラーで落ちる。
- Release 確認時は Rebuild 禁止(自動 Play で確認可。マウスは効くがキー注入は届かない)。
- 生成物は `build/` と各プロジェクトの `Temp/` に集約。遊んでもらう用の配布フォルダは `Tools/MakeGameBuild.ps1`(Release をビルドしてから実行)。
- ゲームは隣のフォルダの BahamutAIMiddleware(BT ライブラリ)を使う。エンジン本体は依存していない。

## ディレクトリ構成(externals 除き約 6.3 万行)

| パス | 役割 |
|---|---|
| `DirectXGame/` | エンジン一式。**ゲーム固有のものを置かない** |
| `DirectXGame/EngineData` | エンジンが自前で持つデータ(シェーダー、既定テクスチャ `white1x1.png`) |
| `DirectXGame/KujataEngine/scene` | コンポーネント基盤の心臓部(Component / GameObject / ComponentFactory / SerializedFieldRegistry / ObjectRef) |
| `DirectXGame/KujataEngine/runtime` | EngineContext / GameModuleLoader(DLL 境界)/ SceneManager / PlayState / TagRegistry |
| `DirectXGame/KujataEngine/components` | 組み込みコンポーネント(Transform / Collider / Rigidbody / Camera / ライト / UI / Particle 等) |
| `DirectXGame/KujataEngine/Editor` | ImGui エディタ一式(Inspector / Hierarchy / Scene・Game ビュー / AnimationWindow) |
| `DirectXGame/KujataEngine/3d` `/2d` `/base` | 描画・D3D12 基盤(DirectXCommon / TextureManager 等) |
| `DirectXGame/KujataEngine/postprocess` | PostEffectPipeline / Volume(HDR / Bloom / Fog) |
| `DirectXGame/KujataEngine/shapes` `/math` | コライダー形状・数学 |
| `DirectXGame/externals` | 外部ライブラリ(imgui / assimp / DirectXTex 等)— 読解・変更の対象外 |
| `Game/` | このリポジトリのゲーム。**フォルダごと持ち出せば切り離せる** |
| `Game/GameModule` | ゲーム DLL のプロジェクト(`GameModule.cpp` がコンポーネント登録の入口) |
| `Game/GameComponents` | ゲーム側ロジック(ボスAI・戦闘・UI 等、約 120 ファイル) |
| `Game/Data` | シーン・プレハブ・リソース・`ProjectSettings` |
| `Game/Game.props` | ゲーム固有のビルド設定(exe 名 `KujataExeName`)。エンジンと GameModule の両方の vcxproj が読む |
| `Sandbox/` | 使い捨ての試作プロジェクト(git 管理外。`--project` で開く) |

## 重要な規約・罠(要点のみ)

- **アセット参照は 2 層**: assetId(`.meta`)+パス fallback。`.meta` は git 管理必須。ID はセット時に自動補完する。
- **半透明は深度を書かない**ため、シーン配列で不透明物より後に置くこと(描画されない原因として非常に読みにくい)。
- **Play の状態持ち越し**: コンポーネントは使い回されるので、非シリアライズ状態は `OnPlayStart` で必ず初期化する。
- GameModule DLL からエンジン側シンボルを使うには `KUJATA_API` エクスポートが必要(未エクスポートだとリンク不可)。
- テクスチャ/フォントの読み込みは描画パス外(Prepare)で行うこと。日本語パスでテクスチャ読込が死ぬ罠あり。
- **エンジンとゲームの境界**: ゲーム固有の設定は `Game/Game.props`(exe 名)と `Game/Data/ProjectSettings/Project.json`(ウィンドウタイトル・背景色)に置き、`DirectXGame/` には書かない。
- **パスの起点は2つ**(`base/ProjectPath.h`): エンジンの持ち物は `GetEngineRoot()` / `GetEngineDataRoot()`、プロジェクトの持ち物(GameModule・Data・Temp)は `GetActiveProjectRoot()` / `GetProjectDataRoot()` を使う。どちらも起動時に一度だけ決まり、キャッシュされる。
- **開くプロジェクトの決まり方**: 起動引数 `--project <フォルダ>` → 無ければエンジンの隣の `Game/` → それも無ければ(配布先)エンジンのフォルダ(= exe の隣)。
- GameModule.dll はプロジェクトの `GameModule/bin/<構成>/` を優先し、無いとき(配布先)だけ exe の隣を読む。
- ゲームコードからエンジンのヘッダは `"components/ImageComponent.h"` のようにインクルードパス基準で書く(`../KujataEngine/...` の相対パスは使わない)。

## 現在の方針(2026-09 時点)

1. **アーキテクチャは OOP+コンポーネントを維持**。ECS への全面書き換えはしない(DOD/ECS の学習は別リポジトリの小品で行う)。
2. エンジン独自の強みとして**決定論+リプレイ**を柱にする(固定タイムステップ+入力列→状態。GOAP/NN エージェントの評価・高速学習・再現デバッグ基盤にするため)。
3. **作業の優先順位**: ① ゲームプロジェクト分離(1リポジトリ=1ゲーム。エンジンを git clone してゲームを作り、`Game/` フォルダで切り離せるようにする)→ ② 決定論+リプレイ(エンジン用リポジトリ KujataEngine で進める)→ ③ コードリーディングと図解(**変更後のコードだけを読む**。変更前との比較はしない)。
   - 分離を先にする理由: 既存ゲームが同居したままエンジンを変更したくない/試作のたびに既存シーンが邪魔になるため。
4. 決定論化の実装では、全体の読解はせず、必要な箇所(更新ループ、rand の呼び出し箇所、deltaTime / 実時間への依存、イテレーション順が不定なコンテナ)だけを的を絞って調べる。

## ドキュメント運用ルール

- **CLAUDE.md(本ファイル)**: AI と共有する前提・規約・現在の方針。方針転換・構成変更・新しい罠の発見時に**その場で更新**する。セッション限りの詳細は書かない。
- **ReadMe.md**: 人間向けの概要・ビルド手順・操作方法。機能追加で操作が変わったら更新。
- **docs/**: 提出資料・設計図解など寄せ集め。コードリーディングの図解成果物はここに `docs/architecture/` として置く。
- コードから自明なこと(クラス一覧・過去の修正履歴)はどのドキュメントにも書かない。git log と実コードを一次情報とする。
