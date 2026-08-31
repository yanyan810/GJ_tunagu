# DebugAI scenarios

Scenario files are JSON documents placed in `scenarios/<GameId>/`.
The Viewer reads the folder name of the configured game project as `GameId`.

Supported scenario fields:

- `id`, `name`, `description`
- `sceneId`: scene that the host should load before running the scenario
- `actor`: `Player`, `Boss`, or `Both`
- `autoRecord`: automatically record replay and coverage while running
- `verifyReplay`: automatically replay the recording and fail on divergence
- `replayVerificationTimeoutSeconds`: maximum time for automatic verification
- `failOnAnomaly`: fail when an anomaly rule with `error` severity is detected
- `timeoutSeconds`
- `goals`

Supported goal types:

- `allActions`: use every Action ID in `actionIds`
- `allActionsWithTag`: use every currently available Action with `tag`
- `anyAction`: use at least one Action ID in `actionIds`
- `phaseReached`: observe `game.phase` equal to `value`
- `enemyDamage`: reduce `enemy.hp` by at least `amount`
- `propertyEquals`: observe the string `property` equal to `value`

The Viewer executes scenarios with the local policy, displays live goal progress,
records an optional replay, updates coverage, and writes a result JSON to
`generated/debug_ai/scenarios/results/` in the configured game project.

`Start One` runs the selected scenario. `Run All` captures the connected game's
current observation as a baseline, restores that baseline before every scenario,
and continues even when an individual scenario fails. Each scenario keeps its own
replay and result JSON. A `batch_*.result.json` file summarizes pass/fail counts,
elapsed time, and every individual result path.

When an anomaly or scenario/replay failure is detected, the Viewer saves up to
three local PNG evidence images per scenario under
`generated/debug_ai/evidence/<ScenarioId>/`. Evidence paths and capture errors are
written to the scenario result and included in the generated HTML test report.
Evidence capture never sends an image to an API.

After `Run All`, the Viewer generates JSON and HTML test reports under
`generated/debug_ai/reports/`. Each report is compared with the newest previous
`report_*.json` and records an optional `comparison` object. The comparison checks
overall and per-scenario pass/fail status, anomaly and replay-verification failure
counts, Action coverage, and material execution-time changes. Its verdict is
`no_baseline`, `stable`, `improved`, or `regressed`. A regression is reported as
diagnostic information and does not independently change the scenario result.
Use the Viewer's `Report` button to open `latest_report.html`.

For a clean batch, open the intended gameplay scene in its initial state before
pressing `Run All`. The game adapter must implement `RestoreDebugObservation` for
baseline restoration; no engine-specific scene manager is required by the Viewer.
