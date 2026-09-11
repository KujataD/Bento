# (ゲーム名)

KujataEngine のテンプレートプロジェクト。新しいゲームを作ったら、このファイルをそのゲームの説明に書き換える
(作り方はリポジトリの `.claude/ReadMe.md` の「新しいゲームを作る」)。

## 必要なもの

- エンジン以外に追加で必要なもの(隣に置くライブラリ等)があればここに書く

## このゲーム固有の設定の置き場所

| 設定 | 場所 |
|---|---|
| exe 名 | `Game/Game.props` の `KujataExeName` |
| ウィンドウのタイトル・背景色 | `Game/Data/ProjectSettings/Project.json` |
| 起動シーン | `Game/Data/ProjectSettings/StartupScene.txt` |
| タグ一覧 | `Game/Data/ProjectSettings/Tags.json` |

## テンプレートに最初から入っているもの

- `Game/Data/Resources/white1x1.png` と `Game/Data/Resources/plane/`: エンジンの `SampleScene` が名前で読むので消さない
- `Game/GameModule/GameModule.cpp` のサンプル Component(MoveForwardComponent / BlinkComponent): 不要になったら消してよい
