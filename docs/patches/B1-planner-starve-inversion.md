# B1 — Planner-starve detection inversion (HIL-pending patch)

**Status:** NOT applied to shipping source. Compile-verified only. Apply on the bench, validate, then land.

## What it fixes

`plan_get_block_buffer_available()` (Planner.cpp:437) returns **free slots**
`(BLOCK_BUFFER_SIZE-1) - queued`, not queued blocks. Three starve-detection
sites compare it as if it were the queued count, so `available < 8` actually
means "queue ≥ 242 ⟺ planner nearly FULL", the opposite of the intent
("when starving, spin fast to shovel BT bytes in"):

- `Serial.cpp:213` — `clientCheckTask` taskYIELD vs vTaskDelay
- `Protocol.cpp:322/330` — 6-pass BT re-feed loop
- `WebUI/BTState.cpp:48` — `bt_planner_is_starving()` (skips blocking BT TX)

Net effect of the bug: at full-rate streaming (planner full) the code busy-spins
and steals core-1 CPU from parsing/planning (creates stutter); at true
starvation (queue tiny) it takes the slow `vTaskDelay` path (anti-starve never
engages). This matches the product's known BT-stutter symptom.

The patch switches all three to `plan_get_block_buffer_count()` (true queued
count) so `count < 8` == genuinely starving. Two other call sites
(`Protocol.cpp:427/736`, named `planner_free`) already use the value correctly
as free-slots and are left unchanged; the `[BT-EOL gap]` diagnostic `B=` field
(Protocol.cpp:154) is intentionally free-slots (matches `Bf:` report) and is
left unchanged.

## HIL validation procedure

1. Flash current shipping build (without patch). Stream a representative page
   over BT; reproduce stutter; capture `[BT-EOL gap=... B=... st=...]` baseline
   (gap > 2s lines indicate stalls). Note `B=` is free-slots.
2. `git apply docs/patches/B1-planner-starve-inversion.patch`, rebuild, flash.
3. Re-run the same page. Compare: expect fewer/smaller `[BT-EOL gap]` events and
   no new stutter at full-rate streaming.
4. Run `agent_gate` (standard) — must stay `overall=pass`.
5. If both improve, land the patch as a normal commit and move B1 from
   "待决策" to a landed invariant in AGENT_HANDOFF §6g.

If validation is inconclusive or regresses, do NOT land — the inversion may be
compensated elsewhere in ways only hardware reveals.
