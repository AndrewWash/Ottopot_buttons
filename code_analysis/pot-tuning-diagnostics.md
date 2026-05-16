# Potentiometer Tuning — Diagnostic Session

> Branch `diag-pot` · debug build (`DEBUG` + `DEBUG_DZ_TUNE`) · knob 0 (CC 9)
> Goal: pin down the residual slow-speed choppiness after v0.3 (#5/#7).

## Observations

### Obs 1 — slow-speed unlock + relock behaviour (2026-05-16)
At very slow speed `unlocked` does **not** rise to 4000 unless some speed
("a burst") is applied to the pot. After unlocking and then attempting fine
adjustments, `unlocked` **returns to 0** mid-gesture.

Interpretation:
- Initial unlock requiring a burst = expected (`dzValue > DZ_UNLOCK_THRESHOLD`
  is the stray-CC guard). Candidate to *soften* later, not a bug.
- `unlocked` dropping to 0 during fine adjustment = **Scenario A**: the deadzone
  relocks mid-gesture. This is the residual choppiness. Fix lives in the
  stay-unlocked logic — `DZ_NET_MOVE_COUNTS` and/or `DZ_PAUSE_TIMEOUT_MS`.

### Obs 2 — relock is near-instant on pause; continuous motion is fine
Continuous back-and-forth motion (no breaks) transmits cleanly at ultra-fine
resolution — `unlocked` holds at 4000. Pausing even briefly drops `unlocked`
to 0 immediately, and re-unlocking needs a fresh speed burst.

Interpretation:
- Resolution during motion is fine → `delta` granularity is OK → the `fmin`
  branch is **deprioritized** (not the bottleneck).
- `lastMovementMillis` is only refreshed at 40 ms (`DZ_NET_WINDOW_MS`) window
  boundaries, and only when that window's net clears `DZ_NET_MOVE_COUNTS` (2).
  A slow turn clears it only intermittently, so the relock timer is chronically
  near-expired *while still turning* → any pause relocks almost instantly.
- Root cause = stay-unlocked margin too thin for slow movement. Levers:
  `DZ_NET_MOVE_COUNTS` (too high), `DZ_NET_WINDOW_MS`, `DZ_PAUSE_TIMEOUT_MS`.

### Obs 3 — relock is speed-gated; the 40 ms window reset is the design flaw
Raw `0pin1` shows smooth ultra-fine movement (~1750 region) — the analog
sensor resolution is excellent. But `unlocked` stays 0 during ultra-fine
movement, and relock is triggered by turn **speed falling below a threshold**,
not by stopping.

Root cause (flaw in the v0.3 #5 design): `netWindowSum` is reset to 0 every
`DZ_NET_WINDOW_MS` (40 ms). A turn slower than ~`DZ_NET_MOVE_COUNTS/40ms`
(~50 counts/s) never accumulates enough net travel within a single window
before the reset discards it → `lastMovementMillis` never refreshes → relock.
The fixed-window reset reintroduced a speed floor.

**Required code change:** replace the fixed-window reset with a *continuous*
net-displacement accumulator — reset it only when it crosses the threshold
(refresh `lastMovementMillis` then), never on a timer. An arbitrarily slow
turn then still accumulates `DZ_NET_MOVE_COUNTS` eventually. Effective speed
floor becomes ~`DZ_NET_MOVE_COUNTS / DZ_PAUSE_TIMEOUT_MS` instead.
Noise protection still holds: if no real movement, the accumulator can't
cross the threshold before `DZ_PAUSE_TIMEOUT_MS` relocks (verify via Check A).

## Measurements

- **Idle `netWindowSum` peak (noise floor): 0** — `delta` is 0 every loop when
  idle. No noise reaches the movement signal at all (median + fmin + fractional
  accumulator scrub it). => the net-displacement discriminator has *no noise to
  fight*; any nonzero `delta` is genuine movement.
- **Freeze test:** `unlocked` drops right at `sinceMove` ≈ 300 ms.
  `DZ_PAUSE_TIMEOUT_MS` is honored correctly — no separate bug.
- Fine-turn `netWindowSum` range: pending Check C (now only confirmatory).
- `0pin1`/`0pin2` resolution: raw trace is smooth at ultra-fine speed — sensor
  resolution is excellent; `fmin` branch not needed.

## Tuning decisions — APPLIED 2026-05-16 (branch diag-pot)

1. **Continuous accumulator** — replaced the fixed 40 ms window: `netAccum`
   sums signed `delta` every loop; when `|netAccum| >= DZ_NET_MOVE_COUNTS` it
   refreshes `lastMovementMillis` and resets. Never reset on a timer.
2. `DZ_NET_MOVE_COUNTS` = **1** — idle noise floor measured at 0, so a single
   count is unambiguously genuine movement. Speed floor ≈ 1 count / 300 ms
   ≈ 3 counts/s (~0.3°/s) — far below any intentional adjustment.
3. `DZ_PAUSE_TIMEOUT_MS` = **300** kept (Check B confirmed it works).
4. `DZ_NET_WINDOW_MS` / `netWindowMillis` / `netWindowSum` — removed.
5. Plotter trace `netWindowSum` → `delta` (netAccum resets every loop at
   threshold 1, so it would always read 0; `delta` is the useful trace).

Files: `OttoPot.h`, `OttoPot.cpp`. Verify by feel in Bitwig.
