# Ghost-CC Tightening Plan (Method 2 follow-up)

> Branch `method-2` · signed unlock accumulator in place · 6-min Bitwig soak
> still produced occasional random CCs accepted as automation. Responsiveness
> is rated "basically perfect" and must be preserved.

## Key finding — the instrumentation has a blind spot

A CC can only leave the device through `sendMidiCC()` at `OttoPot.cpp:295`,
which sits inside `if (!locked)`. **Every ghost CC therefore means some pot was
unlocked.** An unlock is visible for at least `DZ_PAUSE_TIMEOUT_MS` (300 ms), so
it cannot be "too fast to see" on the plotter.

The `DEBUG_DZ_TUNE` plotter block (`OttoPot.cpp:316`) is gated `if (muxc == 0)`
— it prints **knob 0 only**. The other 7 pots (CC 10–16) emit no debug output
at all. The observed ghost is almost certainly on a pot other than knob 0,
which is why watching knob 0's trace shows nothing.

**Conclusion: we cannot tighten correctly until we know which pot ghosts and
its accumulator state at the moment of transmit.** Diagnose first, tune second.

## Phase 1 — Diagnose (behaviour-neutral instrumentation)

All changes in `OttoPot.cpp`, active only under existing `#ifdef DEBUG*`.

1. **Per-CC transmit log — covers all 8 pots.** Inside the transmit block,
   right after `sendMidiCC(...)` at `OttoPot.cpp:295`, add (under `#ifdef DEBUG`):
   ```
   debugln("TX cc:%d k:%d val:%d delta:%d dz:%d pend:%d",
           cc, muxc, value, delta, (int)(dzValue * 100.0f), pendingDelta);
   ```
   During an idle soak this prints **only when a ghost fires** — it names the
   culprit pot and shows `delta` / `dzValue` / `pendingDelta` at that instant.

2. **Make the plotter knob selectable.** Replace the hard-coded `muxc == 0` at
   `OttoPot.cpp:316` with `muxc == DZ_TUNE_KNOB`, and add
   `#define DZ_TUNE_KNOB 0` near the tuning constants. Once Phase 1 names the
   culprit, set this to that knob and watch its full trace.

3. **Re-run the 6-min soak** with `DEBUG` + `DEBUG_DZ_TUNE` enabled, USB Type
   "Serial + MIDI", capturing the serial log to a file.

### What the Phase 1 data decides
- **Same pot every time** → that unit is a marginal/noisy pot; consider a
  per-pot threshold or a hardware look.
- **`delta` at ghost is a small same-direction run (1–2 counts)** → isolated
  noise blip; the directional-persistence gate (Phase 2) fixes it.
- **`delta` is a large jump** → mux cross-channel ghosting or ADC settling, not
  a deadzone problem — different fix (longer settle, extra throwaway read).
- **`pendingDelta` is large at unlock** → buffered run-up is the culprit.

## Phase 2 — Tighten without losing responsiveness (apply after Phase 1)

Recommended primary lever — **directional-persistence unlock gate**:

The signed accumulator already cancels opposite-sign noise, but threshold 1.0f
still unlocks on a single 2-count same-direction blip (or two 1-count loops).
A genuine turn produces a *long run* of same-sign deltas; an isolated blip does
not. So gate the unlock on run length as well as magnitude:

- Track `int dzRun` — the count of consecutive same-sign non-zero deltas.
  `delta == 0` does **not** reset it (slow turns have many zero loops between
  integer crossings); a sign reversal resets it to 1.
- Unlock requires `fabs(dzValue) > DZ_UNLOCK_THRESHOLD` **and**
  `dzRun >= DZ_MIN_RUN` (start `DZ_MIN_RUN = 3`).
- Reset `dzRun = 0` on unlock and on relock, alongside `dzValue`.

Cost to responsiveness: a real turn satisfies a 3-sample run within ~3 ms of
loop time — imperceptible. Benefit: isolated 1–2 sample noise excursions can no
longer unlock, however favourable their sign.

Secondary levers, only if Phase 1 points elsewhere:
- Per-pot `DZ_UNLOCK_THRESHOLD` if one unit is a bad apple.
- Extra ADC throwaway read / longer settle if ghosts show large `delta`.
- Confirm-first-transmit: hold the first post-unlock CC until `dzRun` confirms.

Do **not** simply raise `DZ_UNLOCK_THRESHOLD` — that re-adds slow-turn lag, the
exact regression Method 2 removed.

## Verification

1. Phase 1 build → 6-min idle soak → confirm `TX` lines identify the pot(s) and
   capture their `delta`/`dzValue`/`pendingDelta`.
2. Phase 2 build → repeat 6-min soak → expect **zero** `TX` lines while idle.
3. Slow + fast turns on every knob → CCs still track with no perceptible lag;
   `unlocked` jumps promptly on the plotter for the selected knob.
4. Bitwig acceptance → idle 10 min armed → no new automation lanes created.
