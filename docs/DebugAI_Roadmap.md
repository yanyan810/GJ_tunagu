# DebugAI Kit 仕様メモ

## 目的

DebugAI Kit は、ゲームと AI / BOT / リプレイ / デバッグ検証を接続するための共通基盤です。

最終的な目的は、AI や BOT がゲーム内を自動で動き回り、意図しない挙動や不具合を見つけられるようにすることです。

ただし、目的は自動修正ではありません。

DebugAI Kit が担当する範囲は以下です。

- 現在のゲーム状態を取得する
- ゲーム内の意味を持つ Action を実行する
- 手動プレイや BOT の行動履歴を記録する
- 記録した Action をリプレイする
- 必要に応じて Snapshot から状態を復元する
- AI API に渡せる形で State / Action を JSON 化する
- 異常を検出する
- 調査に必要なログ、レポート、リプレイデータを保存する

## 最終方針

DebugAI は、最初から完全な外部ツールとして作るのではなく、まずは ImGui のように各ゲームへ組み込めるライブラリ形式を目指します。

理由は、ゲームの状態取得、Action 実行、当たり判定、敵やボスの状態、Snapshot 復元などは、ゲーム内部に強く依存するためです。

そのため、まずは以下の形を目指します。

```text
各ゲーム
  |
  +-- DebugAI Kit を組み込む
  |
  +-- ゲームごとの IGameDebugAdapter を実装する
  |
  +-- Update / ImGui / ログ出力などから DebugAIManager を呼ぶ
```

将来的に外部ツールを作る場合も、外部ツールが直接ゲームを触るのではなく、DebugAI Kit にコマンドを送る形にします。

```text
外部 DebugAI Tool
  |
  | DebugCommand JSON / TCP / WebSocket / NamedPipe
  v
ゲーム内 DebugAI Kit
  |
  v
IGameDebugAdapter
  |
  v
実ゲーム
```

つまり、外部ツール化しても、ゲーム内には必ず小さな DebugAI Kit と Adapter が残ります。

AI API を使う場合も同じ方針です。

AI にゲームを直接操作させるのではなく、DebugAI Kit が現在の状態と使用可能 Action を JSON 化し、AI はその中から次の Action を返します。

```text
ゲーム
  |
  v
IGameDebugAdapter
  |
  v
DebugAI Kit
  |
  | State JSON + Available Actions
  v
AI API
  |
  | Action JSON
  v
DebugAI Kit
  |
  v
IGameDebugAdapter::ExecuteDebugAction
  |
  v
実ゲーム
```

AI が返した Action は、必ず `GetAvailableDebugActions()` に含まれるものか検証します。

不正な Action、壊れた JSON、危険な命令、現在実行できない Action が返ってきた場合は、その Action は破棄し、`Wait` や `RandomDebugBot` などの安全な Fallback に切り替えます。

## 目指す使い方

別ゲームへ移植するときは、できるだけ以下の作業だけで使えるようにします。

1. `Engine/DebugAI` 相当のファイルを追加する
2. ゲーム側で `IGameDebugAdapter` を実装する
3. 起動時に `DebugAIManager` へ Adapter を登録する
4. Update で `DebugAIManager::Update` 相当を呼ぶ
5. 必要なら ImGui パネルを呼ぶ
6. ログ出力先を設定する

イメージ:

```cpp
debugAI.SetAdapter(&gameDebugAdapter);
debugAI.SetLogDirectory("generated/debug_ai");
debugAI.SetEnabled(true);

// Game loop
debugAI.Update(deltaTime);
```

ゲーム側が最低限実装するもの:

```cpp
class MyGameDebugAdapter : public IGameDebugAdapter {
public:
    DebugGameState CaptureDebugState() override;
    std::vector<DebugAction> GetAvailableDebugActions() override;
    bool ExecuteDebugAction(const DebugAction& action) override;

    DebugSnapshot CaptureDebugSnapshot() override;
    bool RestoreDebugSnapshot(const DebugSnapshot& snapshot) override;
};
```

## 責務の分離

### DebugAI Kit 側が持つもの

- `DebugAIManager`
- `DebugAction`
- `DebugGameState`
- `DebugIssue`
- `DebugSnapshot`
- `DebugLogger`
- `DebugReplayRecorder`
- `DebugReplayPlayer`
- `RandomDebugBot`
- `ApiDebugBot`
- `IDebugBot`
- `DebugCommand`
- `DebugResponse`
- 共通の異常検出
- Action / State / Snapshot / Replay の JSON 入出力
- 任意の ImGui パネル

### 各ゲーム側が持つもの

- `IGameDebugAdapter` の実装
- ゲーム固有の Action 定義
- ゲーム固有の State 取得
- ゲーム固有の Snapshot 保存 / 復元
- プレイヤー、敵、ボス、シーンなどへの実際の操作
- ゲーム固有の異常検出

### 外部ツール側が将来持つもの

- テストシナリオ管理
- AI / BOT の行動選択
- AI API の設定
- Prompt / State 要約の編集
- リプレイ一覧の管理
- ログビューア
- レポートビューア
- カバレッジ可視化
- 長時間テストの実行管理

## 基本データ

### DebugAction

ゲーム内の意味を持つ操作です。

キーボードやマウス座標を直接保存するのではなく、ゲーム側が理解できる Action として扱います。

例:

```text
MoveLeft
MoveRight
Jump
AttackWeak
AttackSideSpecial
Guard
UseCard
SelectEnemy
EndTurn
TalkToNpc
UseItem
```

最低限持ちたい情報:

- Action 名
- 対象 ID
- 整数パラメータ
- 小数パラメータ
- 文字列パラメータ
- 何フレーム保持するか
- 実行フレーム
- 実行シーン

### DebugGameState

現在のゲーム状態です。

共通で持ちたい情報:

- シーン名
- フレーム番号
- 経過時間
- FPS
- プレイヤー HP
- 敵 / ボス HP
- プレイヤー座標
- 敵数
- 使用可能な Action 一覧
- 乱数 seed
- 現在のリプレイ / 記録状態

ゲームごとに追加したい情報:

- ボスフェーズ
- プレイヤー状態
- 地面にいるか
- 攻撃中か
- 移動ロック中か
- 近くの敵
- 近くの調べられるもの
- 現在エリア ID
- イベントフラグ
- 所持アイテム

### DebugSnapshot

リプレイ開始時や Issue 発生時の状態復元に使う情報です。

最低限保存したいもの:

- シーン名
- フレーム番号
- 乱数 seed
- プレイヤー状態
- 敵 / ボス状態
- 予約スポーン情報
- 重要なゲームフラグ

完全再現を目指す場合に追加したいもの:

- プレイヤー内部タイマー
- 攻撃中、硬直中、無敵時間などの状態
- ボス AI のフェーズ、タイマー、現在行動
- 敵 AI の状態
- 弾、エフェクト、当たり判定の状態
- 乱数生成器の内部状態
- シーン遷移中の状態

Snapshot は全ゲームで完全共通化するのは難しいため、共通の入れ物だけ DebugAI Kit 側に用意し、中身は Adapter 側でゲームごとに保存します。

### DebugIssue

検出された異常です。

例:

- HP が 0 未満になった
- 座標が NaN になった
- プレイヤーがマップ外に出た
- 敵数が不正になった
- 同じ状態が長時間続いた
- シーン遷移が一定時間進まない
- FPS が急に落ちた
- リプレイと現在状態の差分が大きい

保存したい情報:

- Issue ID
- 種類
- メッセージ
- 発生フレーム
- シーン名
- 直前の Action
- プレイヤー座標
- 乱数 seed
- 関連するリプレイファイル
- 関連する Snapshot ファイル

## AI API 連携

AI API 連携では、AI をゲーム専用の直接操作コードにはしません。

AI の役割は、現在の `DebugGameState` と `availableActions` を見て、次に実行する `DebugAction` を 1 つ選ぶことです。

DebugAI Kit 側に `IDebugBot` を用意し、ランダム BOT、カバレッジ BOT、API BOT を同じ入口で扱えるようにします。

```cpp
class IDebugBot {
public:
    virtual ~IDebugBot() = default;
    virtual DebugAction DecideAction(const DebugGameState& state) = 0;
};
```

想定する BOT:

```text
RandomDebugBot
CoverageDebugBot
ReplayDebugBot
ApiDebugBot
```

### AI に渡す情報

AI にはゲーム内部の全情報をそのまま渡すのではなく、判断に必要な情報だけを要約して渡します。

例:

```json
{
  "scene": "GameScene",
  "frame": 1200,
  "goal": "DefeatBoss",
  "player": {
    "hp": 82,
    "position": { "x": 12.4, "y": 0.0, "z": 5.1 },
    "state": "Grounded",
    "canAttack": true
  },
  "enemies": [
    {
      "id": "boss",
      "hp": 360,
      "position": { "x": 18.0, "y": 0.0, "z": 5.0 },
      "distance": 5.6
    }
  ],
  "availableActions": [
    "MoveLeft",
    "MoveRight",
    "Jump",
    "AttackWeak",
    "AttackSideSpecial",
    "Guard"
  ]
}
```

### AI から受け取る情報

AI からは文章ではなく、必ず Action JSON を受け取ります。

例:

```json
{
  "action": {
    "name": "MoveRight",
    "targetId": "",
    "intParam": 0,
    "floatParam": 0.0,
    "stringParam": "",
    "holdFrames": 12
  },
  "reason": "ボスとの距離を詰めるため"
}
```

`reason` はログや調査用であり、ゲーム実行には使いません。

### API BOT の安全ルール

- AI は登録済み Action からしか選べない
- AI が返した Action は実行前に必ず検証する
- 不正 JSON の場合は実行しない
- 使用可能 Action に存在しない場合は実行しない
- API が失敗した場合は Fallback BOT に切り替える
- API 応答待ちでゲーム本体を長時間止めない
- API キーはログやリプレイに保存しない
- AI が選んだ Action と理由はログに保存する

### API 連携の実装順

API 通信そのものは最後でよいです。

先に必要なのは以下です。

1. `IDebugBot`
2. `DebugGameState` の JSON 化
3. `DebugAction` の JSON 化
4. `availableActions` の整理
5. `ApiDebugBot` のインターフェース
6. API 失敗時の Fallback
7. 実際の API 通信

## DebugCommand

将来の外部ツール化を見越して、ImGui 操作やショートカットキーも DebugCommand 経由で実行できるようにします。

これにより、最初はゲーム内 ImGui から呼び、後で外部ツールから同じコマンドを送れるようになります。

想定コマンド:

```text
GetState
GetAvailableActions
ExecuteAction
StartRecording
StopRecording
StartReplay
StopReplay
PauseReplay
StepReplay
SaveSnapshot
RestoreSnapshot
GetIssues
ClearIssues
SetBotMode
SetExternalInputBlocked
```

JSON 例:

```json
{
  "type": "execute_action",
  "action": {
    "name": "AttackWeak",
    "targetId": "",
    "intParam": 0,
    "floatParam": 0.0,
    "stringParam": "",
    "holdFrames": 1
  }
}
```

## リプレイの考え方

リプレイは、入力キーではなく Action を保存します。

よくない例:

```text
KeyDown(Space)
KeyDown(A)
MouseClick(350, 210)
```

望ましい例:

```text
Jump
MoveLeft
AttackWeak
SelectEnemy(enemy_001)
```

Action の方が、UI 変更やキーコンフィグに強く、人間が見ても内容を理解しやすくなります。

ただし、Action だけでは完全再現できない場合があります。

その場合は以下も併用します。

- 初期 Snapshot
- 乱数 seed
- スポーンした敵の実体情報
- ボス AI の内部状態
- プレイヤーの内部状態
- リプレイ中の状態差分ログ

## ログ出力

DebugAI Kit は、最低限以下のファイルを出力します。

```text
generated/debug_ai/
  debug_ai_events.jsonl
  debug_ai_report.txt
  debug_ai_actions_run_0001.jsonl
  debug_ai_snapshot_0001.json
  issues/
    issue_0001_report.txt
    issue_0001_context.json
    issue_0001_replay.jsonl
```

ログの役割:

- `debug_ai_events.jsonl`: 全体のイベントログ
- `debug_ai_report.txt`: 人間が読むサマリー
- `debug_ai_actions_run_XXXX.jsonl`: 手動プレイや BOT の Action 履歴
- `debug_ai_snapshot_XXXX.json`: 状態復元用 Snapshot
- `issues`: 異常発生時の調査パッケージ

## 現在の実装状態

現時点で存在する主なもの:

- `DebugAIManager`
- `DebugAction`
- `DebugGameState`
- `DebugIssue`
- `DebugLogger`
- `IGameDebugAdapter`
- `RandomDebugBot`
- `DebugReplayRecorder`
- `DebugReplayPlayer`
- `GameScene` 用の Adapter 実装
- Action 履歴の記録
- リプレイ再生
- 一部 Snapshot 復元
- ImGui からの DebugAI 操作
- 外部入力遮断
- `generated/debug_ai` へのログ出力

まだ整理したいもの:

- `DebugAI.h` のような単一 include 入口
- `DebugCommand` / `DebugResponse`
- DebugAI Kit とゲーム固有コードの分離
- Snapshot の共通形式
- リプレイ差分検出
- 外部ツール用の通信層

## 実装フェーズ

### Phase 1: 今のゲームで安定させる

目的:

現在のゲームで、手動記録、リプレイ、Snapshot 復元、ログ出力を安定させます。

作業:

- Action ログが 1 プレイごとに分かれるようにする
- リプレイ中に手動入力が混ざらないようにする
- リプレイ中に Action ログが汚れないようにする
- ボス、敵、プレイヤーの Snapshot 復元を増やす
- リプレイと実プレイの差分ログを出す
- ずれたときに原因を追える情報を保存する

完了条件:

- 同じ Action ログで、おおむね同じ流れを再現できる
- ずれた場合に、どのフレームからずれたか分かる
- 既存の通常プレイに影響しない

### Phase 2: DebugAI Kit として整理する

目的:

他ゲームへ移植しやすい形へ整理します。

作業:

- DebugAI の入口を `DebugAI.h` にまとめる
- ゲーム固有コードを Adapter 側へ寄せる
- 共通部分が `GameScene` や `Player` を直接知らない状態にする
- `DebugAIConfig` を作る
- ログ出力先や機能 ON / OFF を設定できるようにする
- Optional な ImGui パネルを分離する

完了条件:

- DebugAI Kit 側だけを別ゲームに持っていける
- 別ゲーム側は Adapter 実装だけで最小動作できる

### Phase 3: DebugCommand を導入する

目的:

ImGui、ショートカットキー、将来の外部ツールが同じ入口から DebugAI を操作できるようにします。

作業:

- `DebugCommand` を定義する
- `DebugResponse` を定義する
- `DebugAIManager::ExecuteCommand` 相当を作る
- ImGui 操作を DebugCommand 経由に寄せる
- リプレイ開始、停止、Snapshot 復元などをコマンド化する
- JSON 変換を用意する

完了条件:

- ImGui からの操作と外部コマンドが同じ処理を通る
- コマンド JSON をログやテストに使える

### Phase 4: 外部接続の準備

目的:

外部ツールから DebugAI Kit を操作できるようにします。

作業:

- 通信方式を決める
- 最初は NamedPipe または TCP を検討する
- `get_state` を外部から呼べるようにする
- `execute_action` を外部から呼べるようにする
- `start_replay` / `stop_replay` を外部から呼べるようにする
- 外部入力遮断を外部コマンドから切り替えられるようにする

完了条件:

- 外部の小さいテストプログラムからゲームを操作できる
- 外部 BOT が Action を選んでゲームを動かせる

### Phase 5: Coverage Bot

目的:

ランダム操作ではなく、未探索の場所や未実行の行動を優先する BOT を作ります。

作業:

- マップをグリッド化する
- 訪問済みセルを記録する
- 未訪問セルへ移動する
- 詰まり検出をする
- 敵や障害物への簡単な対応をする
- カバレッジログを保存する

完了条件:

- ランダム BOT より広い範囲を探索できる
- 探索済み / 未探索の情報をログで確認できる

### Phase 6: API Debug Bot

目的:

AI API を使って、現在のゲーム状態から次の Action を選べるようにします。

作業:

- `IDebugBot` を追加する
- `RandomDebugBot` を `IDebugBot` 経由に寄せる
- `ApiDebugBot` のインターフェースを追加する
- AI に渡す State JSON を作る
- AI から返る Action JSON を検証する
- API 失敗時の Fallback を作る
- AI の選択理由をログに残す

完了条件:

- API 接続なしでも `ApiDebugBot` の入口を差し替えられる
- API から返った Action を安全に実行できる
- 不正な返答が来てもゲーム側が壊れない

### Phase 7: 外部 DebugAI Tool

目的:

ゲーム外からテスト実行、ログ確認、リプレイ管理ができるツールを作ります。

作業:

- テストシナリオ一覧を表示する
- リプレイ一覧を表示する
- Snapshot 一覧を表示する
- Issue 一覧を表示する
- Action を手動送信できるようにする
- BOT / AI 実行を開始できるようにする
- レポートを見やすく表示する

完了条件:

- ゲームを起動したまま、外部ツールから DebugAI を操作できる
- テスト結果を外部ツール上で確認できる

## 移植時の理想フォルダ

将来的には以下のような構成を目指します。

```text
Engine/DebugAI/
  DebugAI.h
  DebugAIConfig.h
  DebugAIManager.h
  DebugAIManager.cpp
  DebugTypes.h
  IGameDebugAdapter.h
  DebugLogger.h
  DebugLogger.cpp
  DebugReplayRecorder.h
  DebugReplayRecorder.cpp
  DebugReplayPlayer.h
  DebugReplayPlayer.cpp
  DebugCommand.h
  DebugCommand.cpp
  DebugJson.h
  DebugJson.cpp
  IDebugBot.h
  RandomDebugBot.h
  RandomDebugBot.cpp
  ApiDebugBot.h
  ApiDebugBot.cpp
  ImGui/
    DebugAIImGuiPanel.h
    DebugAIImGuiPanel.cpp
```

ゲーム側:

```text
Game/Debug/
  GameDebugAdapter.h
  GameDebugAdapter.cpp
  GameDebugSnapshot.cpp
  GameDebugActions.cpp
```

## 未決定事項

- 通信方式は TCP、WebSocket、NamedPipe のどれにするか
- Snapshot の共通 JSON 形式をどこまで決めるか
- ゲーム固有 Snapshot をバイナリにするか JSON にするか
- リプレイはフレーム基準を基本にするか、Action 順基準も持つか
- 外部ツールは C++、C#、Python、Web UI のどれで作るか
- AI API 連携はゲーム内から直接呼ぶか、外部ツール経由にするか
- AI に渡す State をどこまで要約するか
- AI の応答待ちを同期にするか非同期にするか
- 長時間テストを CI で回すか、手元専用にするか
- DebugAI Kit を Debug ビルド限定にするか
- Issue 発生時にスクリーンショットや動画を保存するか

## 直近の優先順位

今のゲームで次にやるなら、以下の順番がよさそうです。

1. リプレイのずれ原因を追える差分ログを強化する
2. ボス、敵、プレイヤーの Snapshot 復元対象を増やす
3. `DebugCommand` / `DebugResponse` を追加する
4. ImGui 操作を `DebugCommand` 経由に寄せる
5. `IDebugBot` を追加して `RandomDebugBot` を差し替え可能にする
6. `DebugGameState` / `DebugAction` の JSON 化を整理する
7. `DebugAI.h` と `DebugAIConfig` を作る
8. DebugAI Kit とゲーム固有 Adapter の境界を整理する

この順番なら、今のゲームで使いながら、将来の移植ライブラリ化と外部ツール化の両方につなげられます。
