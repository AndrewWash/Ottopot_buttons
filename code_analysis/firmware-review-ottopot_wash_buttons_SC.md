# Firmware Code Review — `ottopot_wash_buttons_SC`

> Review date: 2026-05-15 · Branch: `math-check` · Reviewer: Claude Code
> Status: review-only — no code changes made.

## Context

This is a code review of the Teensy 4.0 firmware in
`ottopot_wash_buttons_SC/` — the active folder going forward (`ottopotSC` is
retired). The firmware drives 8 endless rotary potentiometers that send 14-bit
MIDI CC to Bitwig, plus two bank buttons.

The goal is the crispest, most responsive, buttery-smooth pot feel. The original
author noted that "deadzones" took a long time to dial in. This review focuses
on the signal-path math (endless-pot decoding) and the deadzone logic, and ends
with a dedicated investigation into the **slow-turn choppiness** reported (pot
"lags out then starts up again" during fine adjustments).

**Verdict:** It is *not* as good as it's going to get. There are several real
bugs that silently lose or corrupt knob travel, and the slow-turn choppiness is
a fixable design limitation of the deadzone — not noise, not hardware. None of
this is catastrophic (the median filter + deadzone mask it), but each one
chips away at "buttery smooth."

No hardware available for A/B testing right now, so findings below are tagged
**[safe]** (provably correct by analysis, behavior-preserving) vs **[tune]**
(needs flashing + feel testing to set constants).

---

## Summary of findings (prioritized)

| # | Finding | File | Severity | Fix type |
|---|---------|------|----------|----------|
| 1 | Angle-wrap sign flip — 1 backward blip per revolution | EndlessPotentiometer.cpp:50 | High | [safe] |
| 2 | Fractional accumulator discarded on every emit (~10-20% lost travel) | EndlessPotentiometer.cpp:54-62 | High | [safe] |
| 3 | `fmin` of *signed* deltas ≠ min-of-magnitude (intent mismatch) | EndlessPotentiometer.cpp:49 | High | [safe] / [tune] |
| 4 | Deadzone discards the "run-up" travel before unlock | OttoPot.cpp:202-214 | High (feel) | [safe] |
| 5 | **Slow-turn choppiness** — deadzone decay outruns slow movement | OttoPot.cpp:189-216 | High (feel) | [tune] |
| 6 | Deadzone decay coupled to loop timing via integer division | OttoPot.cpp:216 | Medium | [safe] |
| 7 | `delay(5)` adds ~5 ms latency and throttles sample rate | ottopot_wash_buttons_SC.ino:128 | Medium (feel) | [tune] |
| 8 | No ADC settling after mux channel switch → extra input noise | OttoPot.cpp:170-175 | Medium | [tune] |
| 9 | `linearDelta` never initialized in constructor | EndlessPotentiometer.cpp:8-13 | Medium | [safe] |
| 10 | `receivedHSB` / `receivedLSB` uninitialized | OttoPot.h:30-31 | Medium | [safe] |
| 11 | Stale baseline if knob is touched during boot intro | ottopot_wash_buttons_SC.ino:102-107 | Low-Med | [safe] |
| 12 | Cosmetic: no-op atan2 scaling, no-op float math, qsort overkill | several | Low | [safe] |

Items 1, 2, 3, 9 are the core "lost/wrong travel" bugs — fixing them together
should give the biggest single jump in crispness. Item 5 is the slow-turn
investigation (full write-up at the end).

---

## Detailed findings

### 1. Angle-wrap sign flip — High [safe]

`EndlessPotentiometer.cpp:50`
```cpp
if (angle < this->prevAngle)
    linear_delta = linear_delta * -1.0;
```
Direction is decided by comparing two raw `atan2` outputs. `atan2` wraps at
±π, so every full revolution a *continuous forward* rotation produces e.g.
`prevAngle = +3.10`, `angle = -3.10` → `angle < prevAngle` is true → that
forward sample is negated. Result: one wrong-direction blip per revolution,
and because only one rotation direction hits the spurious flip, it also causes
a slow DC drift over many turns. (The wrap-handling block at lines 34-42 only
fixes the unused debug variable — it does *not* feed this sign decision.)

**Fix** — take the sign from a *wrapped* angle delta:
```cpp
float dAngle = angle - this->prevAngle;
if (dAngle >  M_PI) dAngle -= 2.0f * M_PI;
if (dAngle < -M_PI) dAngle += 2.0f * M_PI;
if (dAngle < 0.0f)
    linear_delta = linear_delta * -1.0;
```

### 2. Fractional accumulator discarded on every emit — High [safe]

`EndlessPotentiometer.cpp:54-62`
```cpp
if (this->flinearDelta < 0) linearDelta = ceil(this->flinearDelta);
else                        linearDelta = floor(this->flinearDelta);
if (linearDelta != 0) this->flinearDelta = 0.0;   // <-- discards remainder
```
`flinearDelta` accumulates fractional movement; when it crosses an integer the
integer is emitted and the accumulator is **reset to 0**, throwing away the
fractional remainder. Example: a steady slow feed of `+0.6`/sample → sample 1
`flinear=0.6` emit 0; sample 2 `flinear=1.2` emit `1`, reset → the `0.2` is
lost. Net emitted rate 0.5/sample vs 0.6/sample input ≈ **17% of travel lost**.
Worst near half-count feeds. This is felt as reduced sensitivity / "the knob
doesn't quite reach" — and it directly worsens the slow-turn problem (#5).

**Fix** — subtract the emitted integer instead of resetting:
```cpp
if (this->flinearDelta < 0) linearDelta = ceil(this->flinearDelta);
else                        linearDelta = floor(this->flinearDelta);
this->flinearDelta -= linearDelta;   // keep the fractional remainder
```

### 3. `fmin` of signed deltas ≠ min-of-magnitude — High [safe core] / [tune knob]

`EndlessPotentiometer.cpp:49`
```cpp
float linear_delta =
    abs(fmin(valueA - previousValueA, valueB - previousValueB)) / 2.0;
```
`valueA-prevA` and `valueB-prevB` are *signed*. `fmin` picks the algebraically
smaller one, *then* `abs` is applied. The author's comment (OttoPot.cpp:148)
states the intent is "use the lower of the delta values of both wipers" — but
because the two wipers are ~90° out of phase (quadrature), their per-sample
deltas frequently have opposite signs, and in that case `abs(fmin(...))` returns
the **larger** magnitude, not the smaller. So for roughly 3/4 of the phase
space the code does *not* do what the comment says — it produces a
phase-dependent, inconsistent magnitude. That inconsistency is jitter in the
delta stream.

The math, for context: away from a triangle-wave apex both wipers have equal
slope magnitude, so `|dA| == |dB|` and min vs max doesn't matter. They differ
only when one wiper is *crossing its apex* — there that wiper's measured delta
is compressed (it just reversed direction) and under-reads, while the other
wiper reads the true rate.

- **min-of-magnitude** = the author's stated intent: rejects a glitch on a
  single wiper, at the cost of under-reading near apices (the accumulator in #2,
  once fixed, recovers that travel later).
- **max-of-magnitude** = the more *accurate* instantaneous estimate (ignores the
  apex-compressed wiper) but less noise-resistant.

**Fix** — first make the code actually match the stated intent (consistent
min-of-magnitude):
```cpp
float dA = fabs((float)(valueA - previousValueA));
float dB = fabs((float)(valueB - previousValueB));
float linear_delta = fmin(dA, dB) / 2.0;
```
That single `fmin`→`fmax` token is the accuracy/noise-rejection knob. **[tune]**
Recommend shipping `fmin` first (matches intent, safe), then A/B testing
`fmax` on hardware — it may feel smoother near apices if the median filter is
already absorbing wiper glitches.

### 4. Deadzone discards the run-up travel — High (feel) [safe]

`OttoPot.cpp:202-214` — while `dzValue <= 10` (locked) the per-loop `delta` is
added into `dzValue` but `value` is never advanced. On the loop where `dzValue`
finally crosses 10, only *that loop's* `delta` is applied (`newValue = value +
delta`). All travel accumulated while crossing the 0→10 threshold is silently
dropped, so the parameter visibly lags the knob at the start of every gesture.

**Fix direction** — buffer pending delta while locked, flush it on unlock:
```cpp
// while locked:
pendingDelta += delta;
// on unlock:
newValue = value + delta + pendingDelta;
pendingDelta = 0;
```
Safety: `pendingDelta` is only emitted *after* `dzValue > 10` confirms a
deliberate movement, so this does not weaken the stray-CC / automation-takeover
protection. Discard `pendingDelta` when the deadzone relocks (decays to 0) so a
slow noise drift can't build a latent jump.

### 6. Decay coupled to loop timing — Medium [safe]

`OttoPot.cpp:216`
```cpp
dzValue -= deltaMicros / 5000;   // int / int
```
`dzValue` is `int`, `deltaMicros/5000` is integer division. With `delay(5)` the
loop is ~5-7 ms so this evaluates to 1 (sometimes 0). The decay rate is
quantized and entirely dependent on `delay(5)` existing. If you reduce loop
latency (#7) this truncates to **0 every loop** and the deadzone never
relocks. Make it float + time-based so it's decoupled:
```cpp
// OttoPot.h: float dzValue;  (also float dzMax under DEBUG_DZ_LOGS)
float decay = (float)deltaMicros / 5000.0f;   // ~1.0 per 5 ms, loop-rate independent
dzValue -= decay;
dzValue = max(dzValue, 0.0f);
```

### 7. `delay(5)` latency — Medium (feel) [tune]

`ottopot_wash_buttons_SC.ino:128` — a hard `delay(5)`. Measured budget: the
actual work per loop is only ~0.3-0.5 ms (8 pots × 10 `analogRead` ≈ 0.3 ms;
atan2/float math negligible on the Teensy 4 FPU; LED I²C update is rate-limited
to 60 Hz and costs ~0.5-2 ms only on the loops it fires). **`delay(5)` is
~90-95% of the loop period.** Effective sample rate ≈ 150-200 Hz; knob-to-MIDI
latency ≈ 5-7 ms average, ~10 ms worst case. This is the single biggest latency
lever. But it cannot just be deleted — it is currently load-bearing for the
integer decay (#6) and sets the noise bandwidth the deadzone was tuned against.
**Order:** fix #6 first, then reduce the delay, then re-tune the deadzone.

### 8. No ADC settling after mux channel switch — Medium [tune]

`OttoPot.cpp:170-175` — `mux->channel(muxc)` then immediately 5 reads. Switching
the analog mux connects a new high-impedance pot wiper to the ADC sample/hold
cap; the first conversion can read a blend of the previous channel ("ghosting").
The median-of-5 rejects one bad sample, which is why it works — but it widens
the spread and forces a higher deadzone threshold than necessary. Cheap fix: one
throwaway read after the channel switch:
```cpp
mux->channel(muxc);
(void)mux->read(); (void)mux2->read();   // charge S/H cap to the new channel
for (uint8_t i = 0; i < READS; i++) { pin1[i] = mux->read(); pin2[i] = mux2->read(); }
```
The `<ADC.h>` library is already included in the .ino but unused — widening the
ADC sampling window / enabling hardware averaging there is the more rigorous
option and could replace the qsort median entirely. [tune]

### 9. `linearDelta` never initialized — Medium [safe]

`EndlessPotentiometer.cpp:8-13` — the constructor sets `valueA/valueB/
valueChanged/value`, but **not `linearDelta`**, and the header gives it no
default. It is indeterminate until the first `updateValues()` writes it.
Currently harmless only by call ordering. Add `linearDelta = 0;` to the
constructor (or `int linearDelta = 0;` in the header).

### 10. `receivedHSB` / `receivedLSB` uninitialized — Medium [safe]

`OttoPot.h:30-31` — declared, never initialized. The first inbound CC of a
14-bit pair (`(receivedHSB << 7) + receivedLSB`, OttoPot.cpp:105) uses garbage
for the byte not yet received → one wrong (but clamped) value/LED jump after
boot. Fix: initialize both to 0 in the constructor or header.

### 11. Stale baseline across the boot intro — Low-Med [safe]

`initialize()` calls `updateValues()` once to seed `previousValueA/B`, but the
1600 ms intro animation runs before the first `loop()`. If the knob is moved
during the intro, the first real delta is computed against a stale baseline → a
spurious jump on the first post-intro sample. Fix: re-seed with a fresh
`updateValues()` in the `showintro = false` branch
(`ottopot_wash_buttons_SC.ino:102-107`).

### 12. Cosmetic — Low [safe]

- `EndlessPotentiometer.cpp:25-26` — both `atan2` args are divided by
  `HALF_POT_VALUE`; `atan2` depends only on the ratio, so the common factor
  cancels. No-op (harmless, but misleading).
- `OttoPot.cpp:56-65` `sendMidiCC` — `hsb`/`lsb` are computed by integer
  shift/mask then stored in `float`; `round(floor(...))` / `round(ceil(...))` on
  already-integral values are no-ops. Can be plain `int` math.
- `OttoPot.cpp:179-180` — `qsort` of a 5-element array, 16×/loop. Works, but a
  branchless median-of-5 is faster and drops the `sort_desc` indirection. Minor.
- `EndlessPotentiometer`'s own `value` / `valueChanged` and the wrap block
  (lines 34-42) are debug-only artifacts — `OttoPot` consumes only
  `linearDelta`. Safe to delete once confirmed, which also removes a distractor
  from the real direction logic.

---

## Recommended remediation order (for when you implement)

1. **Batch A — lost/wrong travel (do together, biggest crispness gain):**
   #1 angle-wrap sign, #2 fractional accumulator, #3 `fmin`→min-of-magnitude,
   #9 init `linearDelta`. All [safe].
2. **Batch B — robustness:** #10, #11. All [safe].
3. **Batch C — feel:** #4 run-up buffering [safe], then #6 float decay [safe],
   then #7 reduce `delay(5)` [tune], then #8 ADC settling [tune].
4. **Batch D — slow-turn fix (#5):** see investigation below — this is a
   deadzone redesign and should be done after Batches A/C, with hardware tuning.
5. **Batch E — cosmetic cleanup (#12).**

## Verification (once changes are made)

- Compile in Arduino IDE / `arduino-cli` for Teensy 4.0; confirm no warnings.
- Enable `DEBUG_DZ_TUNE` / `DEBUG_DZ_LOGS` in `main.h` and watch the Serial
  plotter — the author left these graphing constants specifically to tune the
  deadzone. Verify `dzValue` stays above 10 during a slow deliberate turn after
  the #5 fix.
- In Bitwig: assign a pot to a parameter, do a very slow fine adjustment, and
  confirm the parameter tracks smoothly with no stop-start steps.
- Spin a pot through several full revolutions both directions; confirm no
  one-sample reversal blip (#1) and no DC drift.
- Leave all pots untouched for a few minutes; confirm zero stray CCs (deadzone
  still protects automation takeover).

---

## Investigation: slow-turn choppiness ("lags out, then starts up again")

**This is fixable. It is not noise and not the hardware — it is the deadzone
math, by design, failing to distinguish "slow" from "stopped."**

### Why it happens

The deadzone (`OttoPot.cpp:189-216`) gates transmission on `dzValue > 10`:
```cpp
dzValue = max(dzValue, 0);
dzValue += abs(delta);          // accumulate movement
dzValue = min(dzValue, 40);
...
if (dzValue > 10) { /* transmit */ }
...
dzValue -= deltaMicros / 5000;  // decay ~1 per loop (~5-7 ms)
```

So `dzValue` is a leaky bucket: each loop it gains `abs(delta)` and loses ~1.
The pot only transmits while the bucket is above 10. **For the bucket to stay
above 10, incoming movement must out-pace the decay of ~1 per loop.**

Now do the arithmetic for a *slow* turn:

- Decay ≈ **1 per loop**, loop ≈ 5-7 ms → the bucket drains ~**160 units/sec**.
- To stay unlocked you must therefore feed ≥ ~160 counts/sec of `abs(delta)`.
- One full physical revolution ≈ 4095 counts of `linearDelta`.
- 160 / 4095 ≈ **0.04 rev/sec ≈ ~15°/sec** — i.e. you must turn faster than
  about a quarter-turn every 6 seconds, *continuously*, just to keep the pot
  unlocked.

Any fine adjustment slower than that — exactly what you do when dialing in fine
detail — feeds the bucket slower than it drains. `dzValue` sinks below 10, the
pot **locks and goes silent ("lags out")**. You keep turning, `abs(delta)`
slowly refills the bucket past 10, it **unlocks and transmits again ("starts up
again")**, then immediately drains back below 10. That oscillation *is* the
choppiness. The deadzone literally cannot tell a slow deliberate movement from a
stopped knob, because it only looks at movement *rate*, and a slow movement and
noise have a similarly low rate.

**Two existing bugs make it measurably worse:**
- #2 (fractional accumulator reset) throws away ~17% of travel — at slow speeds
  that is ~17% less feed into the bucket, lowering the choppiness threshold
  speed even further.
- #4 (run-up discarded) means every time it re-locks mid-gesture, the travel
  during the re-lock is dropped — so "starts up again" also jumps/skips rather
  than resuming cleanly.

### Is it fixable? Yes.

The root cause is that locking is driven by movement *rate* (leaky bucket of
`abs(delta)`). The correct design **separates the two jobs**:

1. **Unlocking** should still require a deliberate burst — keep the
   rate-threshold for the *initial* unlock. This is what protects against stray
   CCs / automation takeover, and it must stay.
2. **Staying unlocked** should be driven by *time since last genuine movement*,
   not by movement rate. Once unlocked, keep transmitting as long as real
   movement occurred within the last ~250-400 ms; relock only after a true
   pause. The firmware already has an `interactionMillis` timestamp — it is
   currently only used to gate *incoming* CCs (`OttoPot.cpp:98`), not the
   lock/unlock decision. Reusing that concept for relocking is the natural fix.

To keep noise from holding the unlock open forever, the "genuine movement"
test for the timeout should look at **net signed displacement over a short
window** rather than summed `abs(delta)`: real slow rotation is directional and
accumulates net travel, whereas analog noise oscillates around zero and nets to
~0. That is the discriminator the current `abs(delta)` bucket throws away.

Sketch of the redesigned logic:
- Locked → unlocked: same as today (`dzValue` rate burst crosses threshold).
- Unlocked: transmit every nonzero `delta`; whenever a *net* movement is seen,
  refresh a `lastMovementMillis` timestamp.
- Unlocked → locked: only when `currentMillis - lastMovementMillis` exceeds a
  pause timeout (≈300 ms, **[tune]**).

This makes an arbitrarily slow turn stay perfectly smooth (no stop-start)
while preserving the stray-CC protection, because nothing transmits until the
initial deliberate unlock.

**Effort / risk:** moderate — it is a deadzone redesign, ~20-30 lines in
`OttoPot::updateValue` plus 1-2 new state fields. The timeout and the
net-displacement window are **[tune]** constants that need flashing + feel
testing, so this should be done last (Batch D) and tuned on hardware with the
existing `DEBUG_DZ_TUNE` Serial plotter.

**Quick interim relief (not a real fix):** lowering the decay rate and/or the
`dzValue > 10` threshold pushes the choppiness onset to a slower speed, but it
also lets noise hold the unlock open longer — it shifts the problem rather than
solving it. The time-based redesign above is the proper answer.
