# TestBattleScene

- Debug / Development のタイトル・セレクト・図鑑・ゲーム画面から F5 で開く、ボス攻撃専用エディターです。Game Debug Controls の Test Battle Scene ボタンからも開けます。
- 既存の BossTestScene の攻撃編集機能を使います。操作可能な Player、追従カメラ、通常の戦闘進行はありません。
- 船のサイズは Enemy::kDefaultScale、戦闘範囲は OceanBattleFlow::kBattleHalfSize と共通です。現在は240×240で、本編と同じ境界フェンスを表示します。
- W/A/S/D、Q/E、矢印キーは編集用カメラの移動・回転です。Camera の Top / Front などでも観察できます。F1でプレイヤー操作に切り替わることはありません。
- Boss Attacks で機雷・衝撃波・アンカー・スクリュー・Ping Beam のパラメータを編集し、Trigger / Test Fire で確認できます。
- 緑のキューブは照準用デコイです。Ping Beam の Test Player Position で位置を編集します。被ダメージ・死亡はなく、キーボードで操作するプレイヤーではありません。
- Save Boss Attack Settings で resources/Data/BossAttacks.json に保存、Load Boss Attack Settings で再読込します。シーン移動時の自動保存はありません。船の配置・カメラ・デコイ位置はプレビュー用です。
- F2で通常ゲームに戻ります。ReleaseではF5・F7の開発用遷移は無効です。

このエディターは既存の攻撃プレビュー用です。本編の BossBattleTuning.json を編集するF9画面とは別で、保存内容がすべて本編へ反映されるものではありません。

## ボス登場確認（F7）

- Debug / Development のタイトル・セレクト・図鑑・ゲーム・Test画面からF7で開けます。収集時間を省き、本編と同じ登場演出から開始し、その後ボス戦へ進みます。F7を押し直すと登場演出から再開始します。F5の攻撃専用エディターとは別です。
- resources/Music/bossscene/bgm.mp3 は登場開始、boss_move2.mp3 は船の移動開始と6秒の飛び込み、fence_fall.mp3 は2秒の柵落下、sip_landing.mp3 は8.5秒の着水に合わせて再生します。
- 登場演出終了（10秒）で登場BGMを停止し、bossBattleBgm.mp3 をループ再生します。各SEはイベントごとに1回だけ再生し、シーン退出時に停止・解放します。
