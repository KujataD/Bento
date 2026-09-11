# CLAUDE.md — KujataEngine 開発コンテキスト

このファイルは AI アシスタント(Claude Code 等)にプロジェクトの前提を共有するためのもの。
人間向けの概要は [ReadMe.md](ReadMe.md) を参照。

## プロジェクト概要

- **KujataEngine**: DirectX 12 製の自作ゲームエンジン(Unity 風エディタ内蔵)+ ボス戦アクションゲーム。
- 作者は学生(専攻: **ゲームAI**)。応答・コメント・ドキュメントは日本語で書くこと。
- 構成: `KujataEngine.sln` → exe(エンジン/エディタ)+ `GameModule` DLL(ゲームロジック、ホットリロード対応)。

## ビルドと検証

```bash
"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" KujataEngine.sln -p:Configuration=Debug -p:Platform=x64 -m -v:m -nologo
```

- **ユニットテストは運用しない方針**(Tests プロジェクトは削除済み)。検証は「ビルド成功+実機起動」で行う。
- **Component 等の共有ヘッダ(ABI)を変更したら、必ず .sln 経由で exe と GameModule を同時に再ビルド**すること。片方だけ古いと起動時にエントリポイントエラーで落ちる。
- Release 確認時は Rebuild 禁止(自動 Play で確認可。マウスは効くがキー注入は届かない)。
- 生成物は `build/` と `Temp/` に集約。配布用 exe フォルダは `Tools/MakeExeFile.ps1`(Debug 構成必須)。

## ディレクトリ構成(externals 除き約 6.3 万行)

| パス | 役割 |
|---|---|
| `DirectXGame/KujataEngine/scene` | コンポーネント基盤の心臓部(Component / GameObject / ComponentFactory / SerializedFieldRegistry / ObjectRef) |
| `DirectXGame/KujataEngine/runtime` | EngineContext / GameModuleLoader(DLL 境界)/ SceneManager / PlayState / TagRegistry |
| `DirectXGame/KujataEngine/components` | 組み込みコンポーネント(Transform / Collider / Rigidbody / Camera / ライト / UI / Particle 等) |
| `DirectXGame/KujataEngine/Editor` | ImGui エディタ一式(Inspector / Hierarchy / Scene・Game ビュー / AnimationWindow) |
| `DirectXGame/KujataEngine/3d` `/2d` `/base` | 描画・D3D12 基盤(DirectXCommon / TextureManager 等) |
| `DirectXGame/KujataEngine/postprocess` | PostEffectPipeline / Volume(HDR / Bloom / Fog) |
| `DirectXGame/KujataEngine/shapes` `/math` | コライダー形状・数学 |
| `DirectXGame/GameComponents` | ゲーム側ロジック(ボスAI・戦闘・UI 等、約 120 ファイル) |
| `DirectXGame/externals` | 外部ライブラリ(imgui / assimp / DirectXTex 等)— 読解・変更の対象外 |

## 重要な規約・罠(要点のみ)

- **アセット参照は 2 層**: assetId(`.meta`)+パス fallback。`.meta` は git 管理必須。ID はセット時に自動補完する。
- **半透明は深度を書かない**ため、シーン配列で不透明物より後に置くこと(描画されない原因として非常に読みにくい)。
- **Play の状態持ち越し**: コンポーネントは使い回されるので、非シリアライズ状態は `OnPlayStart` で必ず初期化する。
- GameModule DLL からエンジン側シンボルを使うには `KUJATA_API` エクスポートが必要(未エクスポートだとリンク不可)。
- テクスチャ/フォントの読み込みは描画パス外(Prepare)で行うこと。日本語パスでテクスチャ読込が死ぬ罠あり。
- **パスの起点は2つ**(`base/ProjectPath.h`): エンジンの持ち物(エディタ用アイコン、ホットリロード時の SolutionDir)は `GetEngineRoot()`、プロジェクトの持ち物(GameModule・Data・Temp)は `GetActiveProjectRoot()` / `GetProjectDataRoot()` を使う。どちらも起動時に一度だけ決まり、キャッシュされる。
- 開くプロジェクトは起動引数 `--project <フォルダ>` で指定する(未指定ならエンジンのフォルダ)。現状 exe の隣に `GameModule.dll` があるとそちらが優先されるため、複数プロジェクト化の際はビルド後コピーを外すこと。

## 現在の方針(2026-09 時点)

1. **アーキテクチャは OOP+コンポーネントを維持**。ECS への全面書き換えはしない(DOD/ECS の学習は別リポジトリの小品で行う)。
2. エンジン独自の強みとして**決定論+リプレイ**を柱にする(固定タイムステップ+入力列→状態。GOAP/NN エージェントの評価・高速学習・再現デバッグ基盤にするため)。
3. **作業の優先順位**: ① ゲームプロジェクト分離(同一リポジトリ内で複数ゲームを持てる程度。エンジンの SDK 化はしない)→ ② 決定論+リプレイ → ③ コードリーディングと図解(**変更後のコードだけを読む**。変更前との比較はしない)。
   - 分離を先にする理由: 既存ゲームが同居したままエンジンを変更したくない/試作のたびに既存シーンが邪魔になるため。
4. 決定論化の実装では、全体の読解はせず、必要な箇所(更新ループ、rand の呼び出し箇所、deltaTime / 実時間への依存、イテレーション順が不定なコンテナ)だけを的を絞って調べる。

## ドキュメント運用ルール

- **CLAUDE.md(本ファイル)**: AI と共有する前提・規約・現在の方針。方針転換・構成変更・新しい罠の発見時に**その場で更新**する。セッション限りの詳細は書かない。
- **ReadMe.md**: 人間向けの概要・ビルド手順・操作方法。機能追加で操作が変わったら更新。
- **docs/**: 提出資料・設計図解など寄せ集め。コードリーディングの図解成果物はここに `docs/architecture/` として置く。
- コードから自明なこと(クラス一覧・過去の修正履歴)はどのドキュメントにも書かない。git log と実コードを一次情報とする。
