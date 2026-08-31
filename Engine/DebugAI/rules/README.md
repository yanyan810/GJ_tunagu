# DebugAI anomaly rules

Place game-specific JSON in `rules/<GameId>/anomaly_rules.json`. The common
detector evaluates generic `DebugObservation` data and does not reference an
engine vector type or a game-specific State.

Rule fields:

- `id`, `enabled`, `severity`: `info`, `warning`, or `error`
- `scope`: `observation` or `entity`
- `property`: a generic property key; entity scope also supports `position`,
  `velocity`, `id`, `category`, and `type`
- `operator`: `equals`, `notEquals`, `lessThan`, `lessOrEqual`, `greaterThan`,
  `greaterOrEqual`, `isNotFinite`, `outsideRange`, or `unchangedForFrames`
- `value`, or `min` / `max` for `outsideRange`
- `durationFrames`: condition duration before reporting
- `cooldownFrames`: repeated-report suppression
- optional `scenes`, `phases`
- entity selectors: optional `entityId`, `category`, and `type`
- `message`

An `AnomalyDetected` event is written to the replay event timeline. Error-level
anomalies fail a scenario when its `failOnAnomaly` setting is true (the default).
