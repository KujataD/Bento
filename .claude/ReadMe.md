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
   - エディタの `Reload DLL` を押すと、GameModule を `DirectXGame/Temp/HotReload/` へビルドし直して差し替える(ホットリロード)
3. 遊んでもらう用の配布フォルダは、Release をビルドしてから `Tools/MakeGameBuild.ps1` で作る

exe と GameModule.dll は同じ構成(Debug/Release)でビルドすること。違う構成の組み合わせは STL の ABI が食い違って落ちる。

### エディタ操作(Scene ウィンドウ)

- クリックでフォーカスしてから: WASD 移動 / QE 上下 / 右クリックホールド+マウスで視点

### エディタの CUI(コマンドで操作する)

エディタは、コマンドでも操作・確認できる。人間も AI も同じコマンドを使う(設計は [editor-automation.md](editor-automation.md))。

- **Console の入力欄**: エディタの Console ウィンドウの一番下に打つ。`help` で一覧、↑↓で履歴
- **ターミナル**: エディタを起動した状態で `Tools\kujata.cmd <コマンド>`。引数なしで対話モード(`exit` で終了)
  - 空白や `"` を含む引数は、1 行ずつ標準入力で渡すと確実: `echo object.rename MonsterBall "Monster Ball" | Tools\kujata.cmd`
  - `-Json` を付けると返事の JSON をそのまま出す(スクリプト・AI 向け)。失敗すると終了コード 1
- **スクリプト**: `KujataEngine.exe --run <ファイル> --exit` で、1 行 1 コマンドのファイルを順に実行して終了する。結果は `<ファイル>.result.jsonl`、失敗があれば終了コード 1

```
kujata scene.list                                       # 全オブジェクト
kujata object.get MonsterBall                           # フィールドの値
kujata field.set MonsterBall RotatorComponent speed 0.05
kujata play.start
kujata wait 60                                          # 60 フレーム進むのを待つ(その間のログも返る)
kujata undo
kujata view.screenshot game                              # Game ビューを PNG に(Temp/Screenshots/)。editor でエディタ全体
kujata view.colliders on                                # 全 Collider の形を線で描く(F1 と同じ)
kujata state.dump state.json                            # 全オブジェクトの全フィールドを書き出す
kujata log.tail 20 error                                # 最近のエラーだけ
kujata prefab.instantiate Prefabs/Enemy.prefab.json     # プレハブを置く(prefab.list で一覧)
kujata animation.addKey Door RotatorComponent/speed 1 0.5  # 1 秒の位置にキー(animation.save で保存)
kujata schema.get RigidbodyComponent                    # フィールドの型・範囲・説明
kujata material.set Materials/Toon.material.json shaderModel 8  # マテリアルを書き換えて保存(material.list で一覧)
```

- オブジェクトは `親/子` のパスか instanceId で指定する。同じパスが複数あるとエラーになる(instanceId で指定する)
- 変更系のコマンドは Undo できる(履歴のラベルは `[CUI] ...`)
- 返事には毎回、そのコマンドの実行中に出たログと、エディタの状態(モード・シーン・選択・Undo の先頭)が付く
- ログは Console(警告は黄・エラーは赤。上のチェックと入力欄で絞り込める)と、`KujataEngine/logs/editor_<日時>.jsonl`(1 行 1 件の JSON)の両方に出る。エンジン側のログ(シェーダーのコンパイルなど)は、警告とエラーだけが Console にも出る
- プレハブ(`prefab.*`)とアニメーション(`animation.*`)の操作は、Hierarchy・Inspector・Animation ウィンドウのボタンと同じ処理を呼ぶ。プレハブの Revert / Unpack は Undo できる。アニメーションのキーはシーンの Undo では戻らない(`animation.save` で保存)

### エディタのレイアウト

- ウィンドウのドッキング配置・位置・大きさと、各ウィンドウを開いているか閉じているか(Window メニュー)は `KujataEngine/imgui.ini` に保存され、次回起動時に戻る(人ごとに違うので git 管理外)
- 初期配置に戻すときは Window → Reset Layout。`imgui.ini` を消して起動しても初期配置になる
- Inspector のコンポーネントの項目(Transform など)は最初は閉じている。見出しをクリックで開く

### エディタの見た目

- 配色はセージグリーンで統一している(`KujataEngine/Editor/EditorStyle.cpp`)。描画先が sRGB なので、書いた値より画面では明るく出る
- プレハブ(Hierarchy のインスタンス名・Project のプレハブファイル・Inspector の表示)は原色の緑の文字で出る(`EditorStyle::PrefabTextColor()`)

### トゥーン・ローポリ調のマテリアル

Project でマテリアルを選ぶと Inspector に出る。CUI では `material.set <パス> <キー> <値>`(キー名は括弧内)。

- **Shader Model を Toon(`shaderModel` 8)** にすると、明るさを段に分けて塗る(セル調)
  - `Toon Steps`(`toonSteps`): 何段に分けるか(いちばん暗い段と明るい段を含む。4 なら 4 色)
  - `Toon Smoothness`(`toonSmoothness`): 段の境目のぼかし幅。0 でくっきり
- **影の色は世界共通**: Directional Light の `Shadow Color`(`shadowColor`)で決める(マテリアルごとには持たない)。いちばん暗い段の色で、元の色に掛ける。紺などにすると影がやわらかく見える。ライトが暗くても、光の当たる側がこれより暗くなることはない
  - 例: `kujata field.set "Directional Light" DirectionalLightComponent shadowColor [0.22, 0.24, 0.42]`
- どの Shader Model でも使えるもの
  - `Flat Shading`(`flatShading`): 面ごとに平らな陰にする(ローポリの角をはっきり見せる)
  - `Point Sampling`(`pointSampling`): テクスチャをぼかさずに読む(粗いテクスチャをドットのまま見せる)
- 落ち影(物が地面に落とす影)はない。段と影の色は、物体の光が当たらない側(陰)にかかる

### ドット絵化

- メインカメラの CameraComponent の **Pixel Size**(`pixelSize`)を 2 以上にすると、Game ビューの 3D を 1/Pixel Size の解像度で描き、ぼかさずに拡大する(1 ドット = Pixel Size × Pixel Size ピクセル)。1 で通常の解像度
  - 例: `kujata field.set "Main Camera" CameraComponent pixelSize 4`
- Screen Space の UI(Canvas)は拡大の後に元の解像度で重なるので、文字はくっきりしたまま。World Space の Canvas は 3D と一緒にドットになる
- フォグ・ブルームも低い解像度のままかかる(1 ドットの中で色が変わらない)
- 1280×720 を割り切れる値(2 / 4 / 5 / 8 / 10 / 16 / 20)にすると、ドットの大きさがそろう
- Scene ビューはドットにしない(編集しやすさのため)

### 自作シェーダー

マテリアルごとに、自分で書いたシェーダー(`.hlsl`)で描ける。泡・海・雷雲など、標準のシェーダーでは出せない見た目に使う。

1. Material の Inspector の **Custom Shader** で **New** を押す(または `kujata shader.create 名前`)。`Data/Shaders/<名前>.hlsl` にひな形ができて、そのマテリアルで使われる
2. `.hlsl` を書き換えて保存する。エディタが自動で読み直す(手動なら Reload / `kujata shader.reload`)
3. **Param 0〜3**(`shaderParams`。float4 × 4)で、揺れの強さなどの値を渡す(シェーダー側は `gShaderParams.params[0..3]`)

- 1 ファイルに `PSMain`(色を決める。必須)と `VSMain`(頂点を動かす。省略すると標準の処理)を書く。使える値と関数は `KujataEngine/EngineData/shader/Object3dCustom.hlsli` の先頭に書いてある(時間 `gShaderParams.time`、`ToonStep`、影の色など)
- 書き間違えて保存しても止まらない。エラーは Console と Inspector に出て、そのあいだは直前に成功した版(無ければ標準)で描く
- `kujata shader.list` で一覧とコンパイルの成否が分かる
- ゲームのコードから、オブジェクトごとの値を `Model::SetShaderObjectParams`(float4 × 12。シェーダーは `gShaderParams.objectParams`)で渡せる。マテリアルの Param は同じマテリアルの物で共通、こちらはオブジェクトごと
- **半透明として描くとき**(マテリアルの Depth Write が OFF)は、奥にある不透明物までの距離が `SceneDepthBehind(input.position)`[m] で分かる(不透明物を描き終えた時点の深度 `gSceneDepth` を読む)。水の岸の泡・浅瀬の色・物との境目を光らせる、などに使う
- 半透明どうしの描く順番は、ふつうは遠い順。コンポーネントの `GetTransparentQueue()` を小さくすると先に描く(海の面のように、ほかの半透明より奥にある大きな面)
- エンジン同梱の自作シェーダー(`KujataEngine/EngineData/shader/Custom/`)は、Shader の一覧に `engine:Custom/Ocean.hlsl` のように出て、どのプロジェクトからでも選べる。書き方の例としても読める

### 線に沿ったチューブ・リボン(SplineRendererComponent)

点の列をなめらかな曲線でつなぎ、それに沿った筒(Tube)か、カメラを向く帯(Ribbon)を毎フレーム作って描く。放水の水流・紫電・ロープ・レーザーなどに使う。

- 点の渡し方
  - **子オブジェクトの位置**(並び順)。エディタで形を作って確かめるとき
  - **ゲームのコード**から `SetPoints(点の配列, 太さの倍率の配列(省略可))` で毎フレーム渡す(ワールド座標)。渡すと子の位置は使わない(`ClearPoints` で戻る)
- 主な設定: Shape(0=Tube / 1=Ribbon)、Start Width / End Width(太さ。途中はなめらかに変わる)、Sides(断面の角の数。少ないほどローポリ)、Subdivisions(点と点の間の分割数。0で折れ線 = 紫電のカクカク)、Caps(筒の両端をふさぐ)、UV Per Unit(模様を流すときの繰り返し)、Material
- ゲームのコードから `SetShaderUserValue(値)` で、その線だけの値を自作シェーダーへ渡せる(`gShaderParams.userValue`)。マテリアルの Shader Params は同じマテリアルの線で共通なので、線ごとに見た目を変えたいときに使う(例: 先端が物に当たっている水流だけ、先端を欠けさせない)
- 自作シェーダーには曲線の全長が `gShaderParams.curveLength` で渡る。**模様を流すときは UV Per Unit を 1**(u = 根元からの距離[m])にして、模様は距離で、先端の処理は `u / curveLength`(根元0〜先端1)で決める。u を全体で 0〜1(UV Per Unit = 0)にして模様を流すと、線の長さが変わるたびに模様の速さと間隔が変わり、伸び縮みしてがくがく見える
- 形はこのオブジェクト自身の Transform には影響されない(点の位置だけで決まる)
- 当たり判定は持たない(水流の判定は、ゲーム側で水弾の球などで行う)
- パーティクル(ParticleSystemComponent)は `EmitAt(位置, 向き, 個数, 足す速度)` で好きな場所から出せる。水流に沿ったしぶきや、当たった場所の水しぶきに使う

### 海(OceanComponent)

空のオブジェクトに **OceanComponent** を付けるだけで、風のタクト / A Short Hike 風の海が出る(ローポリ・ドット絵向けに、色の変化はすべて段で切り替わる)。

- **形**: 海面の高さの基準はこのオブジェクトの位置。広さは Size X / Size Z、細かさは Divisions(長い辺のマスの数。少ないほどローポリ)
- **Follow Camera**: 板がカメラの真下へついて来る(果てのない海)。波と模様はその場に留まる。板の端は、Volume の Fog で空の色に溶かすと見えなくなる
- **色**: Water Color(沖)・Shallow Color(浅瀬)・Foam Color(泡)・Ring Color(泡の下の暗い輪)。**Lighting** は陰の強さ(0 でべた塗り = 風のタクト、1 でトゥーンの面ごとの陰)
- **泡の模様**(風のタクトの輪): Pattern Size(模様の大きさ[m])・Foam Amount(輪の量)・Foam Width(線の太さ)・Ring Offset(暗い輪のずれ。0 で暗い輪なし)・Distort Strength / Distort Length(模様の揺らぎ)・Drift Speed / Drift Direction(流れ)。模様はワールド座標に貼るので、板が動いてもその場に留まる
- **波の山の泡**: Crest Foam(波の山のどこから上を泡にするか。0 でなし)
- **岸と浅瀬**(A Short Hike): 海の下にある物(地面・岩・浮かぶ物)が近いところは Shallow Color になって透け(Shallow Depth[m] より浅いところを段で、Shallow Alpha で透け具合)、水面との境目は泡になる(Shore Foam[m] が泡の幅。少しずつ打ち寄せる)
- **波**: 正弦波を 4 つまで重ねる。1 つずつ Direction(進む向き[度])・Length(波長[m]。0 で使わない)・Height(高さ[m])・Speed(速さ[m/秒])を決める。見た目の主役は泡の模様なので、うねりは控えめが既定
- 見た目の元は Material でも変えられる(色は掛け算、トゥーンの段・Flat Shading が効く。**BaseColor テクスチャを入れると、その赤が泡の模様になる**。手描きの泡の模様を使うとき)。Shader が空なら海のシェーダーを使う
- 波と模様はゲームの時間で進む(時間スケール 0 のヒットストップで止まる)。Play を始めるたびに同じところから始まる。Play していないときも見た目だけ動く
- **回転は Y 軸まわりだけ、拡大は X・Z だけ**にする(傾けると波が板に沿わない)

**海面の高さを使う**

- 物を浮かべる: **FloatOnWaterComponent** を付ける。Play 中、毎フレームその位置の海面へ Y を合わせ、波の傾きに合わせて傾く(Height Offset で沈み具合、Follow Speed で遅れ(0 でぴったり)、Tilt で傾きの強さ)
- ゲームのコードから: `OceanComponent::TryGetSurfaceHeight(シーン, x, z, 高さ)`(`components/OceanComponent.h`)。海の上なら true と海面の高さを返す(Follow Camera ならどこでも海の上)。描かれている面と同じ高さ(頂点の間は三角形の上の高さ)なので、見た目とずれない。着水・水しぶき・泳ぎの判定などに使う
- 船の航跡は TrailRendererComponent に泡のマテリアルを付けて海面の少し上に引く、着水のしぶきは ParticleSystemComponent の `EmitAt` で出す(どちらも既存の機能)

### 速い物の当たり判定(SphereCast)

`SphereCast(シーン, 前の位置, 今の位置, 半径, 結果, 無視する物)`(`scene/PhysicsQuery.h`)で、球を線分に沿って動かしたときに最初に当たる Collider を調べられる。弾・水弾のように 1 フレームで大きく動く物に使う(点で調べると薄い物をすり抜けるため)。

- 相手は、今ある Collider コンポーネント(球・箱・カプセル)を付けた物なら何でもよい(相手側は何も書かなくてよい)
- 結果には、当たった Collider・オブジェクト・当たったときの球の中心・線分の何割のところか、が入る
- 撃った本人とその子には当たらないよう、無視する物を渡せる

### 半透明の描く順番

深度を書かないマテリアル(Depth Write を切った半透明・加算)とパーティクルは、不透明物をすべて描いた後に、カメラから遠い順に描く。シーンの並び順を気にしなくてよい。

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
| `KujataEngine/` | エンジン本体(scene / runtime / components / Editor / 3d / 2d / base / postprocess / shapes / math / vfx / assets / input、`KujataEngine.vcxproj`、`main.cpp`) |
| `KujataEngine/EngineData/` | エンジンが持つデータ(シェーダー、既定テクスチャ) |
| `externals/` | 外部ライブラリ(imgui / assimp / DirectXTex 等) |
| `DirectXGame/` | このリポジトリのゲーム(GameModule / GameComponents / Data / Game.props / README.md) |
| `Sandbox/` | 使い捨ての試作プロジェクト(git 管理外) |
| `Tools/` | 配布フォルダ作成などのスクリプト |
| `.claude/` | ReadMe・CLAUDE.md と、設計書などのドキュメント(`docs/` は作らない) |
| `build/` | ビルド生成物(git 管理外) |

## ドキュメントの運用方法

このリポジトリのドキュメントは 4 か所で運用する。

1. **.claude/ReadMe.md(本ファイル)** — 人間向けの入口。エンジン共通の概要・ビルド手順・操作方法だけを置く。
2. **[.claude/CLAUDE.md](CLAUDE.md)** — AI アシスタント(Claude Code)と共有する開発コンテキスト。規約・罠・現在の方針を記載し、方針転換や構成変更のたびにその場で更新する。AI とのセッション開始時に自動で読み込まれる。
3. **DirectXGame/README.md** — そのゲーム固有の説明・依存・設定の置き場所。
4. **.claude/ のそのほかのファイル** — 設計書・図解など詳細資料の置き場(例: 決定論の設計 [determinism.md](determinism.md)、ティックとリプレイの詳細設計 [tick-replay.md](tick-replay.md)、全クラスの関係図 [class-map.html](class-map.html)(`python Tools/class_map/build.py` でソースから作り直す)、エディタ操作のコマンド化の設計 [editor-automation.md](editor-automation.md))。`docs/` は作らない。

1 と 2 は全ゲームのリポジトリで同じ内容に保つ(エンジン更新の取り込みで衝突させないため)。
ReadMe はこのファイルに一本化し、ソースのフォルダには置かない(例外はゲーム固有の説明を書く 3 だけ)。
原則: **コードや git log から分かることはドキュメントに書かない**(二重管理で腐るため)。書くのは「コードから読み取れない意図・規約・罠」だけ。
