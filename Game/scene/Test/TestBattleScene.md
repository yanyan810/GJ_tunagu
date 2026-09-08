# TestBattleScene

- 本編の F5、または Game Debug Controls の Test Battle Scene ボタンで開きます。
- F1 で編集用カメラと本編 Player の操作を切り替えます。F2 で本編に戻ります。
- 編集中は W/A/S/D、Q/E と Camera の Top / Front などで観察します。
- 船は Enemy と共通のスケール 6、プレイヤーは本編 Player、水中環境は UnderwaterEnvironment を使用します。プレイヤー操作時の追従距離は本編と同じ 11.5 です。
- Boss Attacks で機雷・衝撃波・アンカー・スクリュー・Ping Beam の範囲を編集し、Trigger / Test Fire で確認できます。Ping Beam は実際の Player の位置を狙います。
- Save Boss Attack Settings で resources/Data/BossAttacks.json に保存します。BossTestScene と共通の設定ファイルです。Load Boss Attack Settings で再読込できます。シーン移動時の自動保存はありません。
- 船の配置・カメラ位置はプレビュー用です。攻撃設定の保存対象には含みません。

このシーンは攻撃範囲の調整用で、Player の被ダメージ判定は行いません。本編 Enemy が現在使っている BossBulletAttack / BossNetAttack と、このエディターの攻撃群は別実装です。保存したプレビュー攻撃を本編の攻撃として実行する接続は別途必要です。
