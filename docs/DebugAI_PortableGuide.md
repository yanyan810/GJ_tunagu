# DebugAI Kit 移植メモ

このメモは、現在作っている DebugAI / リプレイ機能を別ゲームへ移すときの最小手順です。

## 目標

DebugAI Kit は、ゲームごとの処理に深く依存させず、次の作業だけで使える形を目指します。

1. `Engine/DebugAI` をコピーする
2. ゲーム側で Adapter を1つ作る
3. `DebugAIConfig` を少し設定する
4. Update から `InjectAction` / `ProcessAfterUpdate` を呼ぶ
5. 必要なら ImGui からリプレイを選んで再生する

## コピーする共通ファイル

基本的にはこのフォルダごとコピーします。

```text
Engine/DebugAI/
  DebugAI.h
  DebugAIConfig.h
  DebugAIManager.h / .cpp
  DebugTypes.h
  IGameDebugAdapter.h
  IDebugBot.h
  RandomDebugBot.h / .cpp
  ApiDebugBot.h / .cpp
  DebugJson.h / .cpp
  DebugLogger.h / .cpp
  DebugReplayRecorder.h / .cpp
  DebugReplayPlayer.h / .cpp
```

他ゲーム側では、基本的に `#include "DebugAI/DebugAI.h"` だけを入口にします。

## ゲーム側で作るもの

ゲームごとに必要なのは Adapter です。

```cpp
class MyGameDebugAdapter : public IGameDebugAdapter {
public:
    DebugGameState CaptureDebugState() const override;
    void ExecuteDebugAction(const DebugAction& action) override;
    bool RestoreDebugState(const DebugGameState& state) override;
};
```

### CaptureDebugState

現在のゲーム状態を `DebugGameState` に変換します。

最低限あるとよいもの:

- `sceneName`
- `frameNumber`
- `playerHp`
- `enemyHp`
- `enemyCount`
- `playerPosition`
- `randomSeed`
- `availableActions`

リプレイ精度を上げたい場合:

- 敵やボスの `DebugEntityState`
- 予約スポーン
- ボスAIの内部状態
- プレイヤーの重要な内部状態

### ExecuteDebugAction

`DebugAction` をゲーム内の意味ある操作へ変換します。

例:

```cpp
if (action.name == "Move") {
    player.QueueMove(action.intParam);
} else if (action.name == "Jump") {
    player.QueueJump();
} else if (action.name == "AttackWeak") {
    player.QueueAttackWeak();
}
```

マウス座標やキー入力そのものではなく、`Move`、`Jump`、`UseCard`、`SelectEnemy` のようなゲーム内コマンドにします。

### RestoreDebugState

リプレイ開始時に状態を戻す処理です。

最低限:

- frame
- random seed
- player HP
- player position
- enemy HP / enemy count

精度を上げたい場合:

- 敵の位置、速度、AI状態
- ボスのフェーズ、タイマー
- 予約スポーン
- プレイヤーの攻撃状態や無敵時間

## 初期化例

```cpp
#include "DebugAI/DebugAI.h"

DebugAIManager debugAI;

DebugAIConfig config;
config.logDirectory = "generated/debug_ai";
config.detectMapBounds = false; // ゲーム仕様と合わない場合はOFF
config.lowFpsThreshold = 30.0f;

debugAI.Initialize(config);
debugAI.SetAdapter(&myAdapter);
```

## Update で呼ぶもの

リプレイやBot操作を実行する前に:

```cpp
debugAI.InjectAction();
```

ゲーム本体の更新後に:

```cpp
debugAI.ProcessAfterUpdate(deltaTime);
```

手動プレイを録画する場合は、ゲーム側で手動操作を `DebugAction` に変換してから:

```cpp
debugAI.RecordExternalAction(beforeState, action, afterState);
```

## ログ出力

新しい形式では、1回分の記録がフォルダにまとまります。

```text
generated/debug_ai/
  Debug_0001/
    debug_ai_actions.jsonl
    debug_ai_actions_initial_state.json
    debug_ai_events.jsonl
    debug_ai_frames.jsonl
    debug_ai_issues.jsonl
    debug_ai_report.txt
```

`debug_ai_actions.jsonl` がリプレイ本体です。

## 設定で変えられるもの

`DebugAIConfig` で、ゲームごとに検出条件を変えます。

```cpp
DebugAIConfig config;
config.detectMapBounds = false;
config.detectLowFps = true;
config.detectSameState = true;
config.sameStateLimitSeconds = 10.0f;
```

今回のように、ゲーム仕様上マップ外が正常な場合は `detectMapBounds = false` にします。

## 移植時の考え方

共通側に入れるもの:

- Action記録
- リプレイ再生
- JSON出力
- ログ出力
- 異常検出の共通ルール
- Bot / AI API の入口

ゲーム側に残すもの:

- State取得
- Action実行
- Snapshot復元
- ゲーム固有の異常判定
- ImGuiの配置や見た目

この分け方にしておくと、別ゲームでは Adapter と Config だけ差し替える形に近づけられます。
