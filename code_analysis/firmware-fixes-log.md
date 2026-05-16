# Firmware Fixes — Execution Log

> Date: 2026-05-15 · Branch: `blind-fixes` · Author: Claude Code
> Source review: `firmware-review-ottopot_wash_buttons_SC.md` (left unchanged)
> Hardware available: **no** · Build toolchain available: **no**
> (`arduino-cli` / `teensy_loader_cli` not installed)

This log records every change made while executing the firmware review's
findings. All "safe" findings were applied; risky / hardware-tuning-dependent
findings were deferred (see bottom). Changes were verified by code analysis
only — see **Verification still owed** for what the user must do on hardware.

## Files changed

- `ottopot_wash_buttons_SC/EndlessPotentiometer.h`
- `ottopot_wash_buttons_SC/EndlessPotentiometer.cpp`
- `ottopot_wash_buttons_SC/OttoPot.h`
- `ottopot_wash_buttons_SC/OttoPot.cpp`
- `ottopot_wash_buttons_SC/ottopot_wash_buttons_SC.ino`

---

## APPLIED

### #9 — `linearDelta` never initialized · [safe]
`EndlessPotentiometer.h` — `int linearDelta;` → `int linearDelta = 0;`
**Why:** member was indeterminate until the first `updateValues()`. Harmless
today only by call ordering; giving it a default removes the latent UB.

### #3 — `fmin` of signed deltas ≠ min-of-magnitude · [safe core]
`EndlessPotentiometer.cpp` — replaced
`abs(fmin(valueA-previousValueA, valueB-previousValueB))/2.0` with explicit
magnitudes computed first:
```cpp
float dA = fabs((float)(valueA - previousValueA));
float dB = fabs((float)(valueB - previousValueB));
float linear_delta = fmin(dA, dB) / 2.0;
```
**Why:** the wipers are ~90° out of phase, so their signed deltas often have
opposite signs — `abs(fmin(signed,signed))` then returned the *larger*
magnitude for most of the phase space, the opposite of the stated intent.
Now consistently min-of-magnitude.
**Deferred sub-part:** the `fmin`→`fmax` accuracy-vs-noise knob is `[tune]`
and was **not** changed — shipped `fmin` (matches author intent, safe).

### #1 — Angle-wrap sign flip · [safe]
`EndlessPotentiometer.cpp` — direction now taken from a *wrapped* angle delta
instead of `angle < this->prevAngle`:
```cpp
float dAngle = angle - this->prevAngle;
if (dAngle >  M_PI) dAngle -= 2.0f * M_PI;
if (dAngle < -M_PI) dAngle += 2.0f * M_PI;
if (dAngle < 0.0f) linear_delta = linear_delta * -1.0;
```
**Why:** `atan2` wraps at ±π, so a raw `<` comparison negated one forward
sample per revolution and drifted DC over many turns.

### #2 — Fractional accumulator discarded on every emit · [safe]
`EndlessPotentiometer.cpp` — replaced the
`if (linearDelta != 0) flinearDelta = 0.0;` reset with
`this->flinearDelta -= linearDelta;`
**Why:** resetting the accumulator threw away the sub-integer remainder
(~10-20% of slow-turn travel). Subtracting the emitted integer keeps it.

### #10 — `receivedHSB` / `receivedLSB` uninitialized · [safe]
`OttoPot.cpp` constructor — added `receivedHSB = 0; receivedLSB = 0;`
**Why:** first inbound CC of a 14-bit pair combined a real byte with garbage,
causing one wrong (clamped) value/LED jump after boot.

### #11 — Stale baseline across the boot intro · [safe]
`OttoPot.cpp` / `OttoPot.h` — added `OttoPot::reseedBaseline()` (mux-select +
`pot.updateValues(mux->read(), mux2->read())`, the same seeding pattern as
`initialize()`). `ottopot_wash_buttons_SC.ino` calls it for all 8 pots in the
`showintro = false` branch.
**Why:** `initialize()` seeds the baseline once, but the ~1600 ms intro runs
before the first `loop()`; a knob moved during the intro produced a spurious
first delta. Re-seeding at intro end fixes it.

### #4 — Deadzone discards the run-up travel · [safe]
`OttoPot.cpp` / `OttoPot.h` — added `int pendingDelta;` (init 0 in ctor).
While the deadzone is locked, per-loop `delta` is accumulated into
`pendingDelta`; on unlock, `newValue = value + delta + pendingDelta;` then
`pendingDelta = 0`. When the deadzone relocks (`dzValue` decays to 0),
`pendingDelta` is discarded.
**Why:** previously all travel accumulated while crossing the 0→10 lock
threshold was dropped, so the parameter lagged the knob at every gesture
start. Stray-CC protection is unchanged — nothing emits until `dzValue > 10`.
Discarding on relock prevents slow noise drift from building a latent jump.

### #6 — Deadzone decay coupled to loop timing · [safe]
`OttoPot.h` — `int dzValue;` → `float dzValue;`.
`OttoPot.cpp` — `dzValue -= deltaMicros / 5000;` (integer division) →
```cpp
dzValue -= (float)deltaMicros / 5000.0f;
dzValue = max(dzValue, 0.0f);
```
Clamps updated to float literals (`0.0f`, `40.0f`). `DEBUG_DZ_LOGS` /
`DEBUG_DZ_TUNE` blocks cast `dzValue` to `int` where they feed `%d` so debug
builds still compile (`dzMax` kept as `int`).
**Why:** integer `deltaMicros/5000` quantized the decay and depended entirely
on `delay(5)` existing — it would truncate to 0 every loop if loop latency
were reduced. Float + time-based decay is loop-rate independent (~1.0 / 5 ms).
**Note:** average decay is now slightly higher (loop is 5-7 ms → 1.0-1.4 per
loop vs the old constant 1). Behaviour is close but not identical — see
Verification.

### #8 — No ADC settling after mux channel switch · [safe judgment call]
`OttoPot.cpp` — added one discarded read of each mux after `mux->channel(muxc)`
and before the `READS` loop.
**Why:** switching the analog mux connects a fresh high-impedance wiper to the
ADC sample/hold cap; the first conversion can read a cross-channel blend.
**Risk note:** the review tagged #8 `[tune]`, but that tag covers the more
elaborate "widen the ADC sampling window / hardware averaging" option, which
was **not** done. The throwaway read is parameter-free and strictly reduces
ghosting, so it was judged safe to apply now. It does slightly lower the noise
the deadzone sees — if anything that makes the deadzone *more* conservative,
not less, so it cannot cause stray CCs. Flagged here in case the deadzone
feels different on hardware.

### #12 (partial) — Cosmetic, no behaviour change · [safe]
- `EndlessPotentiometer.cpp` — dropped the `/(float)HALF_POT_VALUE` divisor on
  both `atan2` args (atan2 depends only on the ratio; the common factor
  cancels).
- `OttoPot.cpp::sendMidiCC` — `hsb`/`lsb` changed from `float` to `int`;
  removed the no-op `round(floor(...))` / `round(ceil(...))` on already-integral
  shift/mask results.

---

## SKIPPED — deferred, needs hardware

### #5 — Slow-turn choppiness (deadzone redesign) · [tune]
**Not done.** This is a ~20-30 line redesign of the lock/unlock logic plus new
state, and it needs hardware feel-testing with the `DEBUG_DZ_TUNE` Serial
plotter to set the pause-timeout and net-displacement window constants. The
review explicitly schedules it last ("Batch D") and on hardware.
**Troubleshoot later:** with hardware, implement the time-based relock
(transmit while net movement occurred within ~300 ms; relock only after a true
pause) and tune the timeout. Until then, **slow fine adjustments will still
stop-start** — this is expected, not a regression. NB: #2 and #4 (now fixed)
were making #5 measurably worse, so the choppiness onset speed should already
be somewhat lower than before.

### #7 — Reduce `delay(5)` loop latency · [tune]
**Not done.** `delay(5)` is load-bearing: it sets the loop period the deadzone
was tuned against and (previously) drove the integer decay. The review's order
is: fix #6 (done) → reduce the delay → re-tune the deadzone. Reducing it blind
would change the noise bandwidth and feel.
**Troubleshoot later:** on hardware, with #6 already in place, step the delay
down (e.g. 5→2→1 ms or remove), watch the `DEBUG_DZ_TUNE` plotter, and re-tune
the `dzValue > 10` threshold / decay rate against the new noise floor.

### #12 — `qsort` → branchless median-of-5 · [safe but skipped]
**Not done.** Pure micro-optimization; the current `qsort` works correctly. A
hand-written median risks introducing a new bug for zero functional gain.

### #12 — Delete debug-only `value` / `valueChanged` / wrap block · [safe but skipped]
**Not done.** Removing these `EndlessPotentiometer` debug artifacts is a larger
refactor with no functional benefit. Left in place to keep the diff minimal
and the blast radius small. Candidate for a future cleanup pass.

---

## Verification still owed (hardware / toolchain required)

Nothing below could be done locally — no Teensy and no Arduino toolchain.

1. **Compile** in Arduino IDE / `arduino-cli` for Teensy 4.0 — confirm no
   warnings. Pay attention to the new `float dzValue` interactions.
2. **Sanity-compile debug builds** — enable `DEBUG_DZ_TUNE` and `DEBUG_DZ_LOGS`
   in `main.h` and confirm they still build (the `%d` casts were added for
   this).
3. **Revolutions test (#1):** spin each pot through several full turns both
   directions — confirm no one-sample reversal blip and no slow DC drift.
4. **Slow-turn travel (#2/#3):** a slow deliberate turn should now track with
   noticeably less "lost travel" / dead feel. (Stop-start choppiness from #5
   will still be present — expected.)
5. **Gesture start (#4):** the parameter should no longer visibly lag the knob
   at the start of a gesture.
6. **Decay behaviour (#6):** watch `DEBUG_DZ_TUNE` — `dzValue` should decay
   smoothly; confirm the deadzone still relocks when the knob is released.
7. **Idle test:** leave all pots untouched for several minutes — confirm zero
   stray CCs (automation-takeover protection intact).
8. **Boot test (#11):** hold/move a knob during the boot intro — confirm no
   jump on the first post-intro sample.

---

## ROUND 2 — Full potentiometer fix · 2026-05-16

> Branch `blind-fixes`. Build from Round 1 confirmed working on hardware
> ("pretty good but not perfect" — slow fine adjustments still stop-start).
> This round applies the two findings deferred above (#5, #7). Buttons
> back-burnered. Toolchain still not installed locally — compile + tune owed.

### #5 — Slow-turn choppiness (deadzone redesign) · APPLIED
`OttoPot.h` / `OttoPot.cpp`. The single `dzValue > 10` test did two jobs:
detect the initial unlock burst *and* gate staying unlocked. The second is
rate-based, so any turn slower than the decay rate (~15°/sec) drained the
bucket mid-gesture and relocked — the choppiness.
**New state machine** (`updateValue()`):
- New members (`OttoPot.h`): `bool locked`, `unsigned long lastMovementMillis`,
  `unsigned long netWindowMillis`, `int netWindowSum`. Initialized in the ctor
  (`locked = true` — boot silent).
- **Unlock** unchanged: `dzValue` rate burst > `DZ_UNLOCK_THRESHOLD` (stray-CC
  / automation-takeover guard — preserved).
- **Stay unlocked** is now time-based: net *signed* `delta` summed over a
  `DZ_NET_WINDOW_MS` window; if `|netWindowSum| >= DZ_NET_MOVE_COUNTS` it is
  genuine movement and refreshes `lastMovementMillis`. Real rotation nets
  travel; noise oscillates and nets ~0 — the discriminator the old `abs(delta)`
  bucket discarded.
- **Relock** only when `currentMillis - lastMovementMillis > DZ_PAUSE_TIMEOUT_MS`
  (a true pause). On relock: discard `pendingDelta`, clear `dzValue` so
  re-unlocking needs a fresh burst.
- Transmit every unlocked loop the value changes; `interactionMillis` refreshed
  on every transmit so the 100 ms incoming-CC gate stays closed for the whole
  (slow) gesture.
- `pendingDelta` buffering (#4) preserved: buffered only while staying locked,
  so the transmit block `value + delta + pendingDelta` never double-counts.
**Tuning constants** (`#define` block at top of `OttoPot.cpp`):
`DZ_UNLOCK_THRESHOLD 10`, `DZ_PAUSE_TIMEOUT_MS 300`, `DZ_NET_WINDOW_MS 40`,
`DZ_NET_MOVE_COUNTS 2` — last three are `[TUNE]` on hardware.

### #7 — Reduce `delay(5)` loop latency · APPLIED
`ottopot_wash_buttons_SC.ino` — added `#define LOOP_DELAY_MS 1`; `delay(5)` →
`delay(LOOP_DELAY_MS)`. Cuts knob-to-MIDI latency ~5-7 ms → ~1-2 ms. Safe now
that the deadzone decay is time-based (#6) and the new #5 logic uses `millis()`
timeouts. 1 ms keeps the loop period far below `DZ_NET_WINDOW_MS` so the
net-displacement window still spans many samples. Final value is `[TUNE]`.

### `DEBUG_DZ_TUNE` instrumentation extended
`(dzValue > 10)` no longer means "locked", so the plotter block now emits
`unlocked` (true gate), `dzValue*100`, `netWindowSum*100`, and `sinceMove`
(ms since genuine movement). Needs both `DEBUG` *and* `DEBUG_DZ_TUNE` defined
in `main.h`.

### Verification still owed (hardware tuning session)
1. Compile clean for Teensy 4.0, USB Type = MIDI; also build the debug variant.
2. **Noise floor:** idle pots ~1 min, read peak `|netWindowSum|` of pure noise
   on the Serial plotter; set `DZ_NET_MOVE_COUNTS` just above it. Zero stray CCs.
3. **Slow fine turn:** `unlocked` trace stays high the whole turn — no
   stop-start. This is the choppiness fix being verified.
4. **Release:** `unlocked` drops ~`DZ_PAUSE_TIMEOUT_MS` after stopping; tune
   the timeout for feel.
5. Re-check `DZ_NET_MOVE_COUNTS` (the noise floor shifted with the 1 ms delay).
6. Full revolutions both directions — no blip, no DC drift.
7. Dialed in → re-comment `DEBUG`/`DEBUG_DZ_TUNE` in `main.h`, flash production.
