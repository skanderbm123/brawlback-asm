# Continuation brief: Brawlback rollback netcode work

Read this first if you're a fresh Claude Code session (or a human) picking this
up cold. Written 2026-07-22, updated 2026-07-23, 2026-07-27, 2026-07-28. The
user (skanderbm123) will be unreachable / without a PC for about two weeks
starting around 2026-07-22 — this doc exists so work can continue without them
around to give context.

**IMPORTANT constraint set by the user on 2026-07-27: this is fork-only work.
Never open a PR, or otherwise propose merging, into any `Brawlback-Team/*`
repo. All commits stay on `skanderbm123/*` forks. Pulling upstream changes
INTO a fork (fetch + merge) is fine and encouraged; pushing/PRing the other
direction is not, ever, regardless of what any older instruction in this file
might imply.** The issues referenced below (Brawlback-Team's) are read-only
context for prioritization, not something to file or comment on.

## 2026-08-01 session (continued): a second, broader instance of the same race - `remotePlayerFrameData` read from the CPU thread with zero locking, every frame

Immediately after the `async_queue` fix, checked whether the same pattern
existed anywhere else - and it did, on state that's touched far more
often. `remotePadQueueMutex` (`EXIBrawlback.cpp`) already existed and was
correctly held on the *write* side: `ProcessIndividualRemoteFrameData`
(pushing/trimming `remotePlayerFrameData[playerIdx]`), called from
`ProcessRemoteFrameData` on the **netplay thread**, locks it. But grepping
every `remotePlayerFrameData[` access found it read with **zero locking**
from four different functions - `isRollbackMode`, `getRemoteInputs`,
`updateSync`, `GetLatestRemoteFrame` - all reachable from the **CPU/emulation
thread** via `handleFrameDataRequest`, `handleUpdateSync`, and
`handleLocalPadData`. Unlike the `async_queue` race (hit once per
outgoing-send call), these reads happen multiple times per frame, every
single frame, for every remote player - this is hotter and more central
than the fix above.

The read-side call graph nests: `getRemoteInputs` calls `isRollbackMode`;
`updateSync` calls `shouldRollback`, which calls `GetLatestRemoteFrame`.
A plain `std::mutex` would self-deadlock the first time one newly-locked
function called another on the same thread. Changed
`remotePadQueueMutex` from `std::mutex` to `std::recursive_mutex`
(matching the pattern `Netplay.h`'s `async_send_packet_mutex` already
used) specifically so this doesn't depend on having traced every nested
call path with perfect precision - recursive re-entry from the same
thread is safe regardless of exact depth.

**Fix** (commit `de90a5d` on `savestates-efficiency-v2`, NOT pushed - no
fork exists yet): added a `lock_guard<std::recursive_mutex>` at the top of
each of the four read functions, and updated the existing write-side
`lock_guard`'s type to match the mutex's new type.

Between this and the `async_queue` fix right below, that's two confirmed,
live, unguarded cross-thread data races closed this session - both
involving the CPU/emulation thread and the dedicated netplay thread, both
plausible root causes for exactly the kind of intermittent,
hard-to-reproduce crash that's historically hardest to diagnose from bug
reports alone. Worth checking `localPlayerFrameData`/`read_queue_mutex`
next for the same pattern, given how systemic this turned out to be.

## 2026-08-01 session (continued): a real, unguarded data race on `async_queue` between two OS threads - likely the most consequential find this session

Followed through on the "check `Netplay.h`'s `recursive_mutex`" item
flagged at the end of the previous digging round. `BrawlbackNetplay` has
one shared `std::deque<std::unique_ptr<BrawlbackNetPacket>> async_queue`
and one `std::recursive_mutex async_send_packet_mutex` meant to guard it.
`SendAsync` (the push side) correctly locks the mutex around
`async_queue.push_back(...)`. `FlushAsyncQueue` (the pop side) did
**not lock anything at all** around `async_queue.empty()` /
`.front()` / `.pop_front()`.

Traced which threads actually call each: `FlushAsyncQueue` is called
continuously, in a tight loop, from `NetplayThreadFunc` - a dedicated
`std::thread` (`this->netplay_thread`). `SendAsync` is reached via
`BroadcastPlayerFrameDataWithPastFrames`, called from
`handleSendInputs` (`EXIBrawlback.cpp`), which is called from
`handleLocalPadData` - a `DMAWrite` handler, running on Dolphin's
**CPU/emulation thread**, roughly once per simulated frame. (The other
`SendAsync`-adjacent call sites - `BroadcastGameSettings` from inside
`NetplayThreadFunc`/`ProcessNetReceive`, and `BroadcastFramedataAck` which
calls `BroadcastPacket` directly, bypassing the queue - all happen to run
on the netplay thread itself, so they don't add to the race; the
CPU-thread -> netplay-thread crossing via `handleSendInputs` is the one
that matters.)

So `async_queue` was being mutated from **two real, concurrently-running
OS threads** - pushed with a lock held, popped with none. That's a
textbook data race on a `std::deque`'s internal structure: undefined
behavior, not just a style issue, and one that fires on essentially every
frame of every match (since `handleSendInputs` runs continuously and
`FlushAsyncQueue`'s loop never sleeps). This is a very plausible root
cause for exactly the class of symptom that's historically hardest to
pin down from code review alone - sporadic, seemingly-random crashes
during real netplay sessions that don't reproduce reliably, because
they depend on the two threads' exact timing.

**Fix** (commit `f83351e` on `savestates-efficiency-v2`, NOT pushed - no
fork exists yet): locked `FlushAsyncQueue`'s queue access with the same
`async_send_packet_mutex`, scoped tightly around just the
move-out-then-pop (mirroring `SendAsync`'s own existing pattern), so the
actual `BroadcastPacket`/`enet_host_broadcast` call runs without holding
the lock - avoids adding lock-hold time around network I/O while still
closing the race.

Given how directly this maps to "the exact kind of bug that causes
unexplained crash reports during real play," this may end up being the
single highest-value fix from this whole session once the code actually
gets run - can't rank it above the correctness bugs (endianness, off-by-one
memory) with certainty since none of this is build/live-tested yet, but
it's a strong candidate.

## 2026-08-01 session (continued): swept the launcher for the same "checks two of three platforms" pattern that caused the macOS bug - one more found, but it's unreachable

After fixing `handleDolphinExitCode`'s missing `isMac` branch, grepped
every file using `isWindows`/`isLinux`/`isMac` for the same class of gap.
Most checked out fine (`useShortcuts.ts`'s `isMac ? meta : ctrl` is a
correct binary split, not a missing-platform bug; `instance.ts`'s
`isMac`-gated spawn method has a real `else` covering everyone else;
`installation.ts`'s `isLinux`-only extra cleanup step in
`_uninstallDolphin` is correct - Windows/Mac's `userFolder` is already a
subdirectory of `installationFolder` so the first `fs.remove` covers it,
only Linux's `userFolder` lives in a separate `~/.config/...` path needing
its own removal).

One more spot looked like the same bug at first: `isMac ? "app" : "exe"`
extension filters in `Settings/DolphinSettings.tsx` and
`QuickStart/ImportDolphinSettingsStep.tsx` would wrongly filter out real
(no-extension) Linux Dolphin binaries from the file-picker dialog if a
Linux user ever reached them. Traced reachability for both:
`DolphinSettings.tsx`'s copy is wrapped in `{!isLinux && (...)}` (Linux
gets a completely different `DevGuard`-gated directory-picker flow
instead, so this is never rendered there) - not a bug.
`ImportDolphinSettingsStep.tsx`'s copy isn't platform-gated in its own
code, but tracing back through `useQuickStart.ts` to see when that step
ever shows revealed **the whole "migrate from old desktop app" feature is
inert**: it only shows when `useDesktopApp`'s `exists` store is `true`,
that store's initial value is hardcoded `false`
(`useQuickStart.ts:123-124`), and grepping the entire renderer for
`setExists(` found exactly one call - `setExists(false)` inside the step's
own "finish migration" handler, i.e. the *only* code that ever touches
this flag sets it to the same value it already had. Nothing anywhere ever
sets it `true`, so `MIGRATE_DOLPHIN` can never actually be the current
QuickStart step, and this whole component (extension bug included) is
unreachable dead code, not a live bug. Noting the inert feature since it's
a real (if inconsequential) gap, but not touching it - reviving it would
need an actual old-installation detector wired up on the main-process
side, which is a feature to build, not a bug to fix.

## 2026-08-01 session (continued): back to `brawlback-launcher` - macOS users got zero error feedback when Dolphin crashed

Resumed the launcher audit after the deep `EXIBrawlback.cpp`/`TimeSync.cpp`
dig above. `src/renderer/lib/dolphin/handleDolphinExitCode.ts` branches on
`isWindows`/`isLinux` (destructured from `window.electron.common`) to pick
an error message for a nonzero Dolphin exit code - each platform has a
couple of specific known-code messages plus a generic `default:` fallback
("Dolphin exited with error code: 0x..., please post in Discord"). There
was no `isMac` branch at all, even though `window.electron.common` exposes
`isMac` right alongside the other two (`main/api.ts`) - this file just
never destructured or checked it. Result: on macOS, `err` stays `null` for
every single nonzero exit code, Windows/Linux's own `default:` fallback
included - a crash produces zero visible feedback for Mac users, not even
the generic "something went wrong" toast the other two platforms get for
completely unrecognized codes.

**Fix** (commit `e8b5503` on `master`, NOT pushed - no fork exists yet):
added an `isMac` branch using the same generic fallback message
Windows/Linux use in their own `default:` cases - no evidence of
Mac-specific exit codes worth individually special-casing, so this closes
the gap without inventing codes I can't verify. `tsc --noEmit` and the
`husky` pre-commit hooks (lint/format) both passed clean.

## 2026-08-01 session (continued): a third instance of the same root pattern - `latestConfirmedFrame` is also a single shared scalar, and it's the one that actually drives `IncrementalRB::Rollback()`'s target frame

While confirming `updateSync()`'s live rollback-trigger call (see entry
right below), traced `this->latestConfirmedFrame`
(`EXIBrawlback.h:129`) end to end. It's declared as a single `bu32`, not
an array - and `handleUpdateSync`'s loop (`for i in 0..numPlayers { if
(isRollbackMode(frame, i)) updateSync(frame, i); }`) calls `updateSync`
once per *remote* player (the local player naturally never matches
`isRollbackMode`'s check, since `remotePlayerFrameData[localPlayerIdx]`
is never populated - that's local storage, kept in the separate
`localPlayerFrameData` member). In 1v1 there's exactly one remote player,
so `updateSync` runs once per call and this is a non-issue. In a 3-4
player match, `updateSync` runs once per *each* remote player, and every
single call reads and unconditionally overwrites the same
`this->latestConfirmedFrame` (`= finalFrame;` on a match, `= i - 1;` on a
mismatch - both plain assignments, never a min/max reduction across
players). So whichever remote player's `updateSync` call happens to run
*last* in that loop iteration silently clobbers whatever the earlier
players' calls just determined, rather than the shared state ending up as
a true consensus (the minimum confirmed frame across all remote players,
the way `GetLatestRemoteFrame()` already correctly computes elsewhere in
this same file).

This is the same "single shared scalar assumed one remote peer" pattern
as `isPredicting` (fixed earlier this session) and `lastFrameAcked`/
`ackTimers` (documented + leak fixed earlier this session) - but it's the
most consequential instance of the three, because
`this->latestConfirmedFrame` is the literal second argument to
`IncrementalRB::Rollback(locFrame, latestConfirmedFrame)` - the actual
target frame the whole engine rolls back to. In a 3-4 player match, this
value can reflect the wrong player's confirmation state by the time the
engine actually rolls back, meaning `Rollback()` could be told to target
a frame that's wrong for one or more of the players actually involved.

**Not fixed this session** - same reasoning as the other two: this needs
`latestConfirmedFrame` to become per-remote-player (like `isPredicting`
already is, post-fix) *and* the `Rollback()` call site to reduce across
players (taking the true minimum, mirroring `GetLatestRemoteFrame()`'s
existing pattern) rather than using whichever player was processed last -
a real, if bounded, redesign, not a one-line patch. Grouping all three
findings together for whoever tackles 3-4 player support:

1. `isPredicting` (fixed) - was a scalar, now `bool[MAX_NUM_PLAYERS]`.
2. `lastFrameAcked`/`ackTimers` (leak fixed, indexing mismatch documented
   but not redesigned) - indexed by acked-subject instead of
   acking-sender; the leak is gone but the true per-peer ack minimum still
   isn't computed correctly for 3-4p.
3. `latestConfirmedFrame` (this entry, not fixed) - a scalar that needs to
   become per-remote-player, with the `Rollback()` call site changed to
   take a genuine minimum across players instead of "whoever went last."

All three live in `CEXIBrawlback`/`TimeSync` and all three stem from the
same underlying fact: this subsystem's state was designed assuming
exactly one remote peer, and the loops that iterate `0..numPlayers` were
added on top without updating the state shape underneath them to match.

## 2026-08-01 session (continued): confirmed `handleLoadSavestate`/`CEXIBrawlback::LoadState` are dead code - the real rollback trigger is `updateSync()` calling `IncrementalRB::Rollback()` directly

While re-tracing `stopRollbackFrame` (used by `SaveState`'s resim-detection
logic) to make sure its value meant what I assumed, noticed it's written
from two places: `updateSync()` (`this->stopRollbackFrame = locFrame;`)
and `handleLoadSavestate` (from a DMA payload, `CMD_LOAD_SAVESTATE`).
Grepped the ASM repo for who actually sends `CMD_LOAD_SAVESTATE` and
found **nobody** - only `CMD_CAPTURE_SAVESTATE` is ever sent
(`Rollback_Hooks.cpp:338`). `handleLoadSavestate` is genuinely
unreachable, and so is its `IncrementalRB::Rollback(this->lastStatedFrame,
stopRollbackFrame)` call inside it.

The *actual* live rollback trigger is `updateSync()`'s own direct call,
`IncrementalRB::Rollback(locFrame, latestConfirmedFrame)`
(`EXIBrawlback.cpp:356`) - called synchronously from Dolphin's own C++ code
the moment a predicted-vs-actual mismatch is detected, with no DMA
round-trip to the game needed to kick it off. This resolves what briefly
looked like a two-writers race on `stopRollbackFrame`: in practice there's
only one live writer (`updateSync`), so `SaveState`'s resim-detection logic
(`this->framesToAdvance > 1 && frame - 1 < this->stopRollbackFrame`) is
checked and confirmed correct against that single source - `stopRollbackFrame`
holds the pre-rollback "catch-up target" frame, and the condition correctly
identifies frames captured *during* the catch-up resimulation (resim=true,
skip eviction, matching the already-verified `OnPagesWritten` accumulation
logic from an earlier session) versus genuinely new post-recovery frames
(resim=false, evict normally).

Also found `CEXIBrawlback::LoadState(bu32 rollbackFrame)` is declared in
`EXIBrawlback.h` but has **no implementation anywhere** in the `.cpp` -
and is never called either, so it never needed to link. Harmless
vestigial declaration, consistent with this codebase's general pattern of
leftover unused API surface (`Research_Hooks.h`, `test_hooks.h`,
`arena_resize`, `BroadcastPlayerFrameData`, etc., all noted earlier this
session). Not fixed - nothing to fix, just confirming reachability so
this doesn't get mistaken for a live code path in a future audit.

## 2026-08-01 session (continued): the default allowed-stages fallback was including the WRONG Pokemon Stadium - the one with NO rollback fix

While digging through `Matchmaking.cpp`'s `handleMatchmaking()` (parsing
the matchmaking server's JSON response), found the hardcoded default
allowed-stages list (used whenever the server doesn't send an explicit
`stages` array - a genuine fallback path, not dead code):

```cpp
m_allowedStages.push_back(0x2E); // PS2
```

Cross-referenced every stage ID in this list against the real
`Stages::srStageKind` enum in `lib/BrawlHeaders/Brawl/Include/gm/gm_lib.h`
(the same curated header collection that already gave exact confirmation
for the Stadium fix's `grTenganEvent` offsets earlier this session).
**`0x2E` is `Stages::PokemonStadium` / `Stages::DxPStadium`** - the
Melee-imported classic Pokemon Stadium, a "dx" retro-import stage living
in its own separate REL module, `ST_DXPSTADIUM` (module ID 83).
**Pokemon Stadium 2 - the actual native Brawl competitive stage, and the
one `StageFixes.cpp`'s `FreezeStadiumTransform` hook targets via
`ST_STADIUM` (module ID 59) - is `Stages::PokemonStadium2` / `Stages::Stadium`
= `0x14`**, a completely different numeric ID.

The comment's own intent is unambiguous ("// PS2" = Pokemon Stadium 2),
and it's specifically, ironically, exactly the stage this whole session's
extensively-verified Stadium transformation-freeze fix exists to make
rollback-safe. Since `StageFixes.cpp` only hooks module 59
(`ST_STADIUM`), not module 83 (`ST_DXPSTADIUM`), **a match that reached
this fallback list and happened to select stage `0x2E` would hit exactly
the terrain-transformation rollback desync the Stadium fix was built to
prevent - just on the wrong, unfixed stage.** This would have silently
defeated the fix's entire real-world purpose for any match landing here,
which is a genuinely bad way to lose a fix that got this much verification
effort - not because the fix itself was wrong (it wasn't, per the earlier
entries), but because the stage that actually gets selected in this
fallback path was never the one being fixed at all.

**Fix** (commit `df36ff7` on `savestates-efficiency-v2`, NOT pushed - no
fork exists yet): changed `0x2E` to `0x14`.

Spot-checked the rest of the same list against the enum while in there:
`0x1` (Battlefield) and `0x2` (FD) match their real names exactly. `0x5`
("Metal Cavern" in the comment) is actually `Stages::MushroomyKingdom` /
`MarioPast` in the real enum - a real, valid stage, just an odd/incorrect
comment label (not "Metal Cavern," which isn't a real Brawl stage name at
all) - lower confidence this is an actual wrong-*value* bug versus just a
mislabeled comment for the intended stage, so left the value alone.
Same for `0x1C` ("Wario land" in the comment, actually
`Stages::WarioWare`/`Madein` in the enum - a real stage, just casually
misnamed, no Wario Land stage exists in vanilla Brawl to confuse it with).
Only the `0x2E`/"PS2" entry had both an unambiguous intended stage *and* a
numeric value that resolves to a definitively different, real, unfixed
stage - that's what made it fixable with confidence rather than just
flaggable as a naming quirk.

## 2026-08-01 session (continued): root-caused the ack-tracking bug against real Slippi source - it's an indexing-scheme mistranslation, and it also leaks memory unboundedly even in 1v1

Dug further into the ack-tracking finding just below by diffing against
the actual, real, working source it was ported from -
`/workspace/ishiiruka/Source/Core/Core/Slippi/SlippiNetplay.cpp` (the same
repo already used earlier this engagement to find/confirm the `TimeSync.cpp`
wraparound and per-match-reset bugs). This resolved the "why" completely,
and turned up a second, more severe consequence of the same root cause.

**Slippi's real design indexes ack state by *which remote peer sent this
ack*, not by *whose framedata it concerns*.** Line 405:
`u8 pIdx = PlayerIdxFromPort(packetPlayerPort);` - derived from the
*sending* packet's own port/identity - then bound-checked against
`m_remotePlayerCount` (line 406) and used to index `lastFrameAcked[pIdx]`/
`ackTimers[pIdx]` (lines 414/417). Every distinct remote peer gets its own
slot, `0..m_remotePlayerCount-1`, sized purely by remote-peer count with
**no slot for yourself at all**. The push side (`TimeSyncUpdate`'s
Slippi equivalent, line 1303) loops `for (int i = 0; i < m_remotePlayerCount;
i++)` - matching that same remote-only indexing exactly.

**Brawlback's port changed the loop bound from `m_remotePlayerCount` to
`numPlayers`** (`TimeSync.cpp:124`, `TimeSyncUpdate`) - which silently
changes what each index *means*: instead of "the i-th remote peer" it
becomes "the i-th player slot including yourself." The pop side
(`ProcessFrameAck`) never got updated to match this new scheme - it still
only ever writes/reads a single slot, `lastFrameAcked[localPlayerIdx]` /
`ackTimers[localPlayerIdx]`, where that index is always *your own* fixed
subject index (since, per the broadcast-fan-out fact from the entry right
below, every ack that reaches `TimeSync::ProcessFrameAck` is always about
your own sent inputs). The two halves were ported with genuinely
different, incompatible indexing schemes, and nothing enforces they agree.

**Second, more severe consequence found by tracing this precisely: the
now-permanently-mismatched loop bound leaks memory in *every* match,
including 1v1, for the entire match's duration.** `TimeSyncUpdate` pushes
one `FrameTiming` into `ackTimers[i]` for every `i` in `0..numPlayers-1`,
every single call (once per `handleSendInputs`, i.e. roughly every
simulated frame). `ProcessFrameAck` only ever pops from
`ackTimers[this->localPlayerIdx]` - the one slot matching your own index.
Every *other* index's deque (in 1v1: exactly one other index, the remote
player's slot; in 3-4p: up to three) receives a new entry every frame and
**never has anything popped from it, ever, for the rest of the match** -
confirmed by grepping every read/pop site of `ackTimers` in the file (only
the one `ProcessFrameAck` site, gated to `localPlayerIdx`). Over a long
session this grows without bound - not immediately catastrophic in raw
bytes (`FrameTiming` is a small int+int64 pair) but it's a genuine,
confirmed, unbounded per-frame leak that exists in every single match
Brawlback has ever run, not just a 3-4-player edge case.

Also relevant context for whoever eventually fixes this: `EXIBrawlback.h`
has a single `ENetPeer* peer` member (line 89), not an array or a
peer-to-player-index map - so even reproducing Slippi's `pIdx =
PlayerIdxFromPort(...)` approach isn't a drop-in fix here; the current
connection-handling architecture doesn't yet have the infrastructure to
distinguish multiple simultaneous remote peer connections for this
purpose at all. Confirms 3-4 player netplay needs broader connection
-handling work than just this one function, consistent with (and now
better explaining *why*) the project's current focus being 1v1.

**Update: fixed the leak specifically, separately from the deeper
redesign** (commit `4da11e2` on `savestates-efficiency-v2`, NOT pushed -
no fork exists yet). Since `ackTimers[i]` for `i != localPlayerIdx` is
proven (by exhaustive grep) to never be read by anything, restricting
`TimeSyncUpdate`'s push to just that one index is strictly
behavior-preserving - it cannot change any observable behavior, since
nothing was ever reading what got removed. Changed `TimeSyncUpdate`'s
signature to take `localPlayerIdx` (it previously had no way to know its
own index at all - `TimeSync` doesn't store one), updated its one call
site in `handleSendInputs` to pass `this->localPlayerIdx`, and gated the
`ackTimers[i].push_back(timing)` line behind `if (i == localPlayerIdx)`.
Left `lastFrameTimings[i]` populated for all `numPlayers` indices
unchanged (it's a fixed-size array, not a growing container, so writing
unused slots there is wasted work but not a leak - narrower scope for
this specific fix, not worth touching in the same change).

This fixes the confirmed memory leak in every match. It does **not** fix
the deeper per-remote-peer indexing mismatch itself - `ProcessFrameAck`
still can't distinguish which of several remote peers sent a given ack in
a 3-4 player match, so `getMinAckFrame`'s over-reporting problem (detailed
in the entry below) is unchanged. That part genuinely needs the
per-remote-peer redesign (matching Slippi's `PlayerIdxFromPort()`-derived
indexing) and the `ENetPeer* peer`-is-a-single-member connection-handling
work noted above - left undone, still the right call given it needs a
real design, not a patch.

## 2026-08-01 session (continued): found a real, structural 3-4-player-only bug in ack tracking - documented, NOT blindly fixed (needs a real redesign, not a line patch)

Following the `ProcessFrameAck` broadcast-fan-out finding above, dug into
what `lastFrameAcked`/`ackTimers` (`TimeSync.h`) actually track, since
`getMinAckFrame` (used by `handleSendInputs` to decide which of our own
old local inputs are safe to stop sending, because everyone's confirmed
receipt) is named like it should take a true minimum across remote peers.

`lastFrameAcked` and `ackTimers` are both indexed `[MAX_NUM_PLAYERS]`, but
by *subject* (whose framedata is being acked), not by *which remote peer
sent this specific ack*. Given the broadcast-fan-out fact established
above - the ack's `playerIdx` is always the acked-framedata's original
sender, and (per `CEXIBrawlback::ProcessFrameAck`'s upstream filter) that
always equals `this->localPlayerIdx` by the time it reaches
`TimeSync::ProcessFrameAck` - **every remote peer's ack about my own
inputs lands in the exact same slot**: `lastFrameAcked[this->localPlayerIdx]`.

In a 1v1 match this is harmless - there's only one remote peer, so
"whichever ack arrived" and "the one peer's ack" are the same thing. In a
3-4 player match, each of the 2-3 other remote peers independently acks my
inputs, and `ProcessFrameAck`'s `this->lastFrameAcked[localPlayerIdx] =
frame > lastAcked ? frame : lastAcked;` is a plain running max across
*all* of them combined into one shared slot - so if peer 1 (fast
connection) has acked through frame 100 but peer 2 (slow connection) has
only acked through frame 50, this reports 100, not the true minimum (50)
across peers. `getMinAckFrame`'s "minimum" is real only across *subject
indices* (which, again, in a 3-4 player match, are mostly permanently 0
for indices other than my own - since nothing ever writes
`lastFrameAcked[remotePeerIdx]` for an index that isn't mine), not across
the actual distinct peers sending those acks.

**Concrete consequence**: `handleSendInputs`'s own comment states its
purpose plainly - "we send *all* unacked inputs so that when the remote
client doesn't receive inputs, and needs to rollback the next packet will
have all the inputs that that client hasn't received." If
`getMinAckFrame()` over-reports (thinks frame 100 is acked by everyone
when only the fast peer has, and the slow peer is still at 50), old
frames 51-100 stop being included in future outgoing packets *before* the
slow peer has actually confirmed receiving them - if that peer's own
local copy of those frames was ever lost (packet drop, since acks/framedata
travel over `ENET_PACKET_FLAG_UNSEQUENCED`/similar unreliable channels),
there's no path left to resend them. `ackTimers`'s ping/RTT bookkeeping has
the same conflation problem for the same reason.

**Why not fixed this session**: this needs a real per-remote-peer index
(`lastFrameAcked[MAX_NUM_PLAYERS][MAX_NUM_PLAYERS]` or similar, keyed by
[which peer sent this ack][whose framedata it's about], with
`getMinAckFrame` changed to take a genuine min across the *senders* who
matter) rather than a one-line patch - the current single-slot-per-subject
layout is baked into `FrameOffsetData`/`ackTimers`/`lastFrameAcked` all
being sized `[MAX_NUM_PLAYERS]` uniformly, and reworking it risks the same
"could silently trade one bug for a worse one" risk already flagged for
the 2026-07-31 thread-lifecycle finding. Given this project's current
focus is 1v1 (where this is provably harmless), this is real but lower
urgency than anything actually fixed this session - documenting clearly so
whoever tackles 3-4 player support next has the actual mechanism already
identified rather than having to re-derive it from symptoms.

## 2026-08-01 session (continued): fixed a real, live logging bug - `ProcessFrameAck`'s ERROR log fires constantly (and incorrectly) in any 3-4 player match

Traced `BroadcastFramedataAck` (`EXIBrawlback.cpp:590`) back to its caller
in `ProcessRemoteFrameData`: `playerIdx` passed to it is
`mostRecentFramedata->playerIdx` - the *original sender's* own player
index, not the local receiver's. That's the correct design for
identifying "this ack is about player X's framedata" - but
`BroadcastFramedataAck` sends it via `netplay->BroadcastPacket(...)`,
which goes through `BrawlbackNetplay::BroadcastPacket` ->
`enet_host_broadcast` - a genuine broadcast to *every* connected peer, not
a point-to-point send back to the original sender only.

In a 1v1 match there's exactly one other peer, so this never surfaces: the
one peer who receives the ack is always the one it's about.
In a 3-4 player match, every peer receives every other peer's acks too -
e.g. player C's ack (for player A's framedata) gets delivered to B and D
as well, who have no use for it. `ProcessFrameAck`
(`EXIBrawlback.cpp:720`) checks `frameAck->playerIdx != this->localPlayerIdx`
before acting on an ack - correct gating logic - but logged
`ERROR_LOG_FMT(BRAWLBACK, "FrameAck playeridx is not local player idx! (This
is wrong...)")` on the "not for me" branch. That branch is *routine*,
*expected* traffic in any >2 player match (every peer hits it for every
ack not about their own framedata), not an error - left as-is, this would
flood error-level logs constantly in any FFA/doubles match, burying real
problems, and its own wording ("This is wrong...") actively misleads
whoever's debugging based on the logs into thinking something's broken.

**Fix** (commit `715e651` on `savestates-efficiency-v2`, NOT pushed - no
fork exists yet): downgraded to `INFO_LOG_FMT` with accurate wording
("Ignoring FrameAck for player N (not us)"), plus a comment explaining the
broadcast-fan-out reasoning so nobody re-introduces the alarming framing
later. Purely a logging/observability fix - the actual gating logic
(`if/else` routing to `this->timeSync->ProcessFrameAck`) was already
correct and untouched.

## 2026-08-01 session (continued): re-verified `TimeSync::calcTimeOffsetUs`/`ProcessFrameAck`, found a dead-code protocol mismatch in `Netplay.cpp`

Hand-traced `TimeSync::calcTimeOffsetUs`'s trimmed-mean (sort each player's
offset buffer, discard bottom/top third via `(int)((1/3)*bufSize)`,
average the middle) through several buffer sizes (1, 2, 3, 4, 30) - all
correct, including the small-buffer edge cases (integer truncation makes
the "trim" a no-op below size 3, which is the right degenerate behavior,
not a bug). The `if (count <= 0) return 0;` guard is provably unreachable
(count = bufSize - 2*floor(bufSize/3) is always > 0 for bufSize >= 1) but
harmless. Also confirmed `ReceivedRemoteFramedata`'s `frameDiffOffsetUs =
USEC_IN_FRAME * (timing.frame - frame)` now benefits directly from today's
earlier `MS_IN_FRAME`/`USEC_IN_FRAME` fix - this is the actual call site
whose precision that fix improves.

While re-reading `Netplay.cpp` fresh, found `BroadcastPlayerFrameData`
(singular - sends `[CMD_FRAME_DATA][raw PlayerFrameData]`, no count byte)
doesn't match what `ProcessNetReceive`'s `CMD_FRAME_DATA` handler always
expects (`u8 numFramedatas = data[0]; PlayerFrameData* framedata =
(PlayerFrameData*)&data[1];` - always reads a count byte first). Sending
via this function would have the receiver misinterpret
`PlayerFrameData::randomSeed`'s first byte as a framedata count and parse
complete garbage from there. Checked reachability: zero call sites for
`BroadcastPlayerFrameData` anywhere - only `BroadcastPlayerFrameDataWithPastFrames`
(which correctly includes the count byte) is ever actually called, from
`EXIBrawlback.cpp:561`. Dead code, not a live bug, but a genuine
near-miss - if anyone ever "simplified" the single-framedata case back to
calling this function without noticing the missing count byte, it would
break every packet. Noting it rather than fixing an unreachable function,
consistent with this session's standing "don't patch what nothing calls"
practice, but flagging it since it's the kind of trap worth deleting
outright in a future cleanup pass rather than leaving armed.

## 2026-08-01 session (continued): audited Brawlback's modifications to stock Dolphin files (not just the dedicated `Brawlback/` tree) - clean

Grepped all of `Source/Core/Core/` for "Brawlback"/"BRAWLBACK" outside the
dedicated `Brawlback/` directory and `HW/EXI/EXIBrawlback.*` to find every
place Brawlback touched Dolphin's *own* pre-existing files - this is
higher-risk territory than Brawlback's own new code, since a subtle
regression here could affect other EXI devices/config, not just Brawlback
itself. Found four: `HW/EXI/EXI_Device.h`/`.cpp` (registers the
`EXIDeviceType::Brawlback` enum value and its `CEXIBrawlback` factory
case), `Config/MainSettings.cpp` (`MAIN_SLOT_B`'s default device type),
`ConfigManager.cpp` (default replay directory).

All four are minimal and correct: the new enum value is appended last
before the special `None = 0xFF` sentinel (no collision), the fmt
formatter's `names` array has exactly 15 entries matching the 15
non-`None` enum values in the same order (a classic bug spot - add an
enum value without a matching formatter entry and every subsequent
value's displayed name shifts by one; not the case here), the factory
switch case has its own `break` (no fallthrough), and the
`EnumFormatter<...::Brawlback>` template parameter (which bounds the
formatter's valid range) was correctly updated to point at the new last
value instead of the old one. `MAIN_SLOT_B`/`ConfigManager` changes are
plain, uncontroversial default-value assignments. No bugs found in
Brawlback's footprint inside stock Dolphin.

Also spot-checked a handful of `InitState`'s hardcoded `ExcludeMem`
addresses (Data/BSS/stack, PAD OSAlarm, etc.) against `RSBE01.lst` -
`0x80009760` (the exclude range's start) matches a real exception-table
symbol right at the start of static data, and `0x805bacc0` ("PAD OSAlarm")
lands exactly on `g_PadSystem`'s own base address (consistent with an
`OSAlarm` being a real, small leading member of that struct, not a
red flag). The others land in expected "anonymous buffer, no nearby named
symbol" gaps (Sound, CopyFB, RenderFifo) - normal for opaque allocated
buffers, not something a symbol database would be expected to name.

## 2026-08-01 session (continued): traced and confirmed the live `GetPhysicalRegions` callback wiring - previously an unverified black box

`incremental_rb.cpp`'s live `InitState` `#else` branch (the actual code
path, given `MULTITHREAD`/`SPECIFIC_TRACKING`/`HEAPS` are all confirmed
undefined) calls `cbs.getPhysicalRegions()` to discover which memory
regions to `TrackAlloc` for dirty-page rollback tracking - this callback
had never been traced to its actual implementation this session. Grepped
`EXIBrawlback.cpp` first (nothing - wrong file) then found the real wiring
in `Memmap.cpp`: `MemoryManager::Init()` populates a 4-slot
`m_physical_regions` array (`[0]` RAM always-active, `[1]` L1 cache
always-active, `[2]` fake-VMEM only for non-Wii/non-MMU GameCube mode,
`[3]` EXRAM only when `wii=true`) *before* calling `IncrementalRB::InitState(cbs)`
later in the same function - so by the time the callback fires, `.active`
flags are already correctly set. For Brawl (a real Wii title), that
resolves to RAM + L1 cache + EXRAM tracked, fake-VMEM correctly excluded.
The callback chain itself (`IncrementalRB::GetPhysicalRegionsCb` ->
`cbs.getPhysicalRegions` -> free function `getPhysicalRegions()` in
`Memmap.cpp` -> `MemoryManager::GetPhysicalRegions()`, a simple reference
accessor `{ return m_physical_regions; }`) is a clean, correct, fully-traced
pipeline. No bug; this closes out a previously-unverified piece of the live
tracking setup.

While in there, spot-checked `getGameMemFrame()`'s hardcoded
`0x901812b4` against `RSBE01.lst`'s `g_GameFrame` entry
(`0x901812A0`) - confirms it's `g_GameFrame + 0x14`, a real object base
plus a plausible field offset (matches `updateFrameCounter`'s
`g_GameFrame.persistentFrameCounter` reference in `Rollback_Hooks.cpp` -
same struct, presumably that field, though neither `doldecomp-brawl` nor
the more complete `ssbb-decomp` has `GameFrame`'s member layout decompiled
yet to confirm the exact field). Not contradictory, but not fully closed
either - noting the boundary of what's currently verifiable via source
rather than treating "plausible" as "proven."

## 2026-08-01 session (continued): confirmed brawlback-asm's own source tree is fully covered; two more vestigial headers noted

Listed every `.cpp`/`.h` in `brawlback-asm` outside vendored dependencies
(`lib/BrawlHeaders`, `lib/brawlback-common`, `lib/Syriinge`, the vendored
LLVM toolchain under `tools/`) - confirms the project's own source is
exactly the set of files already covered this session and in prior ones:
`Brawlback-Online/source/*.cpp` and their headers.

Noticed two headers with zero implementation and zero call sites anywhere:
`Research_Hooks.h` (declares a `Research::` namespace with
`dump_gfMemoryPool_hook`/`alloc_gfMemoryPool_hook`/etc. - note the
*different* namespace from the actually-referenced-but-commented-out
`Match::dump_gfMemoryPool_hook` etc. in `Rollback_Hooks.cpp`'s
`InstallHooks()`, confirming these are unrelated, never-implemented
declarations, not just another reference to the already-documented dead
heap-tracking pipeline) and `test_hooks.h` (`TestHooks::InstallHooks()`).
Grepped all of `source/*.cpp` for both namespaces - no matches anywhere.
Fully vestigial, orphaned headers; not worth touching.

This closes out `brawlback-asm`'s own source tree as fully read and
audited this engagement (every fix this session on this repo -
`EXIPacket::CreateAndSend`'s NULL check - is already committed and pushed).

## 2026-08-01 session (continued): found `/workspace/ssbb-decomp` has per-REL-module symbol tables - re-verified every `*Rel` hook address this session could previously only guess at

Big discovery while chasing down the Stadium fix's remaining unverified
piece: `/workspace/ssbb-decomp` (the user's own fork, `skanderbm123/ssbb-decomp`,
upstream `doldecomp/brawl` - a more complete checkout than
`/workspace/doldecomp-brawl`, 426 decompiled `.cpp` files vs. 187) has
`config/RSBE01_02/rels/<module_name>/symbols.txt` for **every** REL module
in the game, including exactly the ones Brawlback's `*Rel` hooks target:
`st_stadium`, `sora_melee`, `sora_scene`, `sora_menu_main`,
`sora_menu_sel_char`, `sora_menu_sel_stage`. Earlier this session I noted
"no way to verify [REL-relative jump targets] further without a REL-specific
symbol map I don't have access to" - that map exists, just not somewhere
I'd looked yet.

Wrote a small per-module symbol lookup (parses `name = .section:0xADDR;
// type:T size:0xN` lines, section-aware) and re-checked ~30 `*Rel` hook
addresses across all 6 of those modules against it. Two standout results:

- **`stStadium::update`** (the Stadium fix's hook point,
  `Modules::ST_STADIUM` offset `0x27A8`): the symbol at that *exact*
  address in `st_stadium/symbols.txt` is `update__9stStadiumFf` - the
  literal mangled name for `stStadium::update(float)`. This is stronger
  confirmation than the decompiled-source match noted above: an actual
  linker-derived symbol name confirming both the function's identity and
  that the hook lands precisely on its entry point, not a guess or a
  disassembly-inferred offset.
- **`SkipDirectlyToCSS`** (`Modules::SORA_MENU_MAIN` offset `0x2E4F8` - the
  hook whose *type* I fixed earlier this session, `sySimpleHookRel` ->
  `syInlineHookRel`): confirmed the *address* is also correct - an exact
  function-start match (`fn_2_2E4F8`, unnamed but a real, `0x29C`-byte
  function boundary), not a random or mid-function offset. Extra
  confidence on top of the earlier hook-type fix.

**Caught and fixed a bug in my own verification script along the way**:
my first pass parsed every symbol regardless of ELF section into one
address-sorted list, which is wrong - `.text`, `.bss`, `.rodata`, `.data`
etc. all have their own small offset numbering that can numerically
overlap (e.g. `.text:0x8b78` and some unrelated `.bss:0x8b78` are
completely different real addresses). This produced one false alarm
(`fixEffects3` appeared to hook a `.bss` *object*, i.e. data, not code -
which would've been a real, serious bug if true: patching a branch
instruction into a data location and jumping to it). Filtering to
`.text`-only before doing nearest-symbol lookup fixed it: `fixEffects3`
correctly lands inside a real, 0x450-byte `.text` function. Worth noting
as a methodology lesson - re-ran every other result from this pass through
the corrected, section-aware version too, and all of them held up.

All ~30 checked `*Rel` hooks land inside real `.text` functions in their
respective modules (several as exact function-start matches, matching
their `syInlineHookRel` auto-return semantics; others mid-function,
matching their naked+manual-`bctr`-resume `sySimpleHookRel` counterparts).
No new bugs, but this closes out essentially all remaining doubt about
whether Brawlback's REL-relative hook table is patching the right
addresses at all - a class of error none of today's other checks could
have caught.

## 2026-08-01 session (continued): independently re-verified the actual "Stadium fix" this branch is named after - checks out exactly

Given the branch name (`claude/brawlback-stadium-fix-y15twm`), gave
`StageFixes.cpp` (the file implementing that literal fix) a fresh,
independent check with today's tools rather than trusting the earlier
build/commit alone. The fix (`FreezeStadiumTransform`, hooked at
`Modules::ST_STADIUM` offset `0x000027A8`) forces
`*(u32*)(stStadiumSelf + 0x288 + 0xA4) = 1` - i.e. sets some object's
`m_state` field to `1` at a computed offset, with an inline comment
claiming `0x288` is `m_event1` (a `grTenganEvent`) within `stStadium`,
`0xA4` is that class's own `m_state` field offset, and `1` is
`grTenganEvent::Running`.

Found `/home/user/brawlback-asm/lib/BrawlHeaders/Brawl/Include/gr/gr_tengan_event.h`
- a real, curated header for exactly this class, with a
`static_assert(sizeof(grTenganEvent) == 172, ...)` pinning its layout.
It explicitly documents `State m_state; // +0xA4` and `enum State {
NoEvent = 0, Running = 1, ReadyEnd = 2 }` - an **exact** match, both the
offset and the enum value, for the two components of this fix that are
independently checkable against a real, size-asserted header (rather than
just re-reading the same comment that was already in the file). The third
component - that `m_event1` really sits at `+0x288` within `stStadium`
itself - had no available header in what I'd checked so far.

**Then found it anyway**: `/workspace/ssbb-decomp` is the actual "brawl"
decomp repo the original fix's writeup (way below, "What's been done: the
Stadium transformation freeze fix") already cited by path
(`src/mo_stage/st_stadium/st_stadium_update.cpp`) - it's present in this
environment, just not somewhere I'd looked yet this session.
`include/st_stadium/st_stadium.h` has the **exact real class**:

```cpp
class stStadium : public stMelee {
public:
    bool m_unk1d8;               // 0x1d8
    grTenganEvent m_event0;       // 0x1dc
    grTenganEvent m_event1;       // 0x288
    ...
};
static_assert(sizeof(stStadium) == 0x7c4, "Class is wrong size!");
```

`m_event1` at `+0x288` - **exact match**, and this class also carries its
own `static_assert`-pinned size. `st_stadium_update.cpp`'s actual
`stStadium::update()` body is a word-for-word match for the control flow
described in the original writeup (`if (m_event0.isReadyEnd()) { if
(!m_event1.isEvent()) { ... m_event1.start(); ... } }`), and critically,
line 69-70 (`m_event0.update(deltaFrame); m_event1.update(deltaFrame);`,
unconditional, every call) **confirms** the original writeup's untested
inference that `m_event0` needs no special handling once `m_event1` never
starts - it just keeps ticking normally regardless.

That leaves exactly one open item from the original writeup: *"Haven't
verified `stStadium::update`'s calling convention preserves `r3` ... through
Syringe's inline-hook trampoline."* Checked this too, directly in
`lib/Syriinge/include/sy_core.h`'s `InlineHook` constructor - the
trampoline's fixed instruction template is:

```
instructions[2] = 0xBC61000C;  // stmw r3, 0xC(r1)   <- saves r3..r31
... (branch to hook) ...
instructions[9] = 0xB861000C;  // lmw r3, 0xC(r1)    <- restores r3..r31
```

`stmw`/`lmw` (Store/Load Multiple Word) save and restore **every** GPR from
r3 through r31 around the hook call, by definition of the instruction. This
isn't circumstantial - it's the literal PowerPC encoding, decodable without
needing to run anything. `r3` (the `this` pointer, per standard PPC calling
convention) is provably preserved both *into* `FreezeStadiumTransform`
(so `stStadiumSelf` is genuinely the right pointer) and *back out* to the
resumed original code.

**All four claims underlying this fix are now independently confirmed
against real source** (three against decompiled game code, one against
the hooking framework's own source) - as much confidence as static
analysis can give without an actual Dolphin+ISO test run, which nobody in
this fix's history (this session or the original one) has had access to.

## 2026-08-01 session (continued): symbol-cross-referenced ~25 absolute-address hooks against real doldecomp-brawl symbols - no bugs, strong confirmation the hooks target the right functions

Used `/tmp/.../scratchpad/symlookup.py` (from earlier this engagement) to
look up every absolute-address (non-`*Rel`) hook in `InstallHooks()`
against both symbol databases. Wrote a small Python regex extractor to
list every `(address, hookType, function)` tuple straight from the source
rather than transcribing by hand.

Found strong, specific corroboration that hooks land on exactly the
functions their names claim to intercept - e.g. `Match::setRandSeed` hits
`generate__6mtRandFv` (mtRand's `generate()`) exactly at its start;
`FrameAdvance::updateLowHook` hits `updateLow__11gfPadSystemFv`;
`FrameLogic::gfTaskProcessHook`/`2` hit `process__6gfTaskFQ26gfTask11ProcessType`
exactly at its start and +4; `FrameLogic::fixEffects5`/`6` land inside
`setSkipLowEffect__FPvUi` (literally an "effect" function);
`NetMenu::setFixStaleInputsTrue` lands inside `updateLowGC__11gfPadSystemFP11gfPadStatus`.
Also noticed the whole `beginningOfMainGameLoop`/`moveUpdateSystem`/
`setFrameAdvanceCounter`/`fixEffects4`/`beginningOfFrameLoop` hook family
all land at ascending offsets (0x0, 0x18, 0xf8, 0x10c, 0x19c) inside one
real function, `startOfGameLoop__Fv` - and separately, `setToLoggedIn`/`2`,
`forceFriendCode`, `forceConnection`, `netThreadTaskOverride`/`2` (6 hooks
total) all land inside one single unnamed 0x500-byte function
(`fn_8014B364`) at ascending offsets matching their plausible semantic
order (connection -> friend code -> logged-in -> thread override). Both
are strong structural confirmations that these hook families are
coherently patching one real function at multiple relevant points, not
scattered/misplaced addresses.

Also double-checked `ReplaceTrainingRoomText`/`ReplaceTrainingRoomText2` -
these looked unusual at first (both registered `syInlineHook`, rather than
the usual naked+`sySimpleHook`/plain+`syInlineHook` alternating pair seen
everywhere else), but both are genuinely plain functions with no manual
resume, so both correctly use the auto-returning hook type - not a bug,
just a case where two independent injection points in a small function
both happen to want the same hook type.

Tried the same technique on the naked functions' *embedded jump targets*
(e.g. `BBisCompleteMeleeSettingAllMember`'s hardcoded resume address
`0x809644D0`) but these all landed in the `0x8096xxxx`-`0x809Fxxxx` range,
well outside the static DOL's address space that `symbols.txt`/`RSBE01.lst`
actually cover - almost certainly because these are `*Rel`-hooked
functions whose resume targets point into the dynamically-loaded
`SORA_MELEE.rel` module's own runtime code, which isn't in either symbol
database (those only cover the main executable). No way to verify these
further without a REL-specific symbol map I don't have access to - noting
the limitation rather than guessing.

No new bugs from this pass; strong additional confidence that hook target
addresses are pointing at the intended real functions across the board.

## 2026-08-01 session (continued): real, build-verified, pushed fix on brawlback-asm itself - `EXIPacket::CreateAndSend` missing NULL check

While re-verifying EXI packet framing (`exi_packet.cpp`) after the
`GameSettings` endianness finding - wanted to make sure the actual wire
framing itself (cmd byte + payload size) matched what Dolphin's `DMAWrite`
parsing expects, which it does, cleanly, no bug there - noticed
`EXIPacket::CreateAndSend(unsigned char EXICmd, void* source, unsigned int
size)` calls `MemExpHooks::mallocExp(new_size)` and immediately
`memmove`s into the result with **no NULL check**, unlike the two
`EXIPacket` constructors right above it in the same file, which do check
(that was itself an earlier-session fix - they used to log the wrong,
not-yet-initialized `size` member instead of the actual attempted alloc
size on failure, but they did always check for NULL).

Checked reachability: `CreateAndSend` is called 16 times across
`Rollback_Hooks.cpp` and is the **only** send mechanism actually used
anywhere - grepped for direct `EXIPacket` construction (the
constructor+`.Send()` API) and found zero real instantiation call sites,
confirming that whole API is dead code (matches earlier findings about
this file generally). Every live piece of EXI traffic - `CMD_ONLINE_INPUTS`
every frame, `CMD_FRAMEDATA` every frame, `CMD_FRAMEADVANCE` every frame,
`CMD_START_MATCH`, `CMD_FIND_OPPONENT`, etc - goes through this one
un-checked function. `mallocExp` is a thin wrapper directly around the real
`MEMAllocFromExpHeapEx` Revolution SDK allocator, which can and does return
NULL under heap exhaustion/fragmentation - a real possibility over a long
play session sharing the heap with normal Brawl gameplay allocations. A
failure here would NULL-pointer-crash the whole game on the very next
`memmove`, instead of gracefully dropping just that one packet the way the
(unused) constructors already handle it.

**Fix**: added the same null-check-and-log pattern the constructors use.
**This is build-verified** (unlike everything else in this session's log,
which is all on local, unpushed Dolphin-fork/launcher clones) - ran the
real `python3 ./bbk.py setup && make` toolchain, links and outputs
`Brawlback-Online.rel` cleanly with only pre-existing unrelated
`__declspec` warnings. Committed as `8b98972` directly on
`claude/brawlback-stadium-fix-y15twm` and already pushed (this is
`brawlback-asm` itself, not a fork-in-waiting like the Dolphin/launcher
work).

## 2026-08-01 session (continued): re-audited `GameLoop`/`GetInputsForFrame`/`getGamePadStatusInjection`/`setFrameAdvanceFromEmu` - no new bugs, one more dead-code lead ruled out

Read through the core ASM-side simulation loop (`FrameAdvance::GameLoop`,
`GetInputsForFrame`, `ProcessGameSimulationFrame`, `getGamePadStatusInjection`,
`setFrameAdvanceFromEmu`, `FrameLogic::beginningOfMainGameLoop`) end to end.
All consistent with the established swap-on-receive convention
(`GetInputsForFrame` calls `Util::FixFrameDataEndianness` after reading from
Dolphin; `setFrameAdvanceFromEmu` calls `Utils::swapByteOrder` after reading
`framesToAdvance`). Re-confirmed `GameLoop` line 979's `>> 2 & 1 != 0` is the
same already-ruled-out false alarm from an earlier session (`X & (1!=0)`
reduces to `X & 1`, same truth value intended).

Noticed `FrameLogic::ShouldSkipGfTaskProcess` uses `strstr(nonResimTasks,
taskName)` (a hardcoded allowlist string as the haystack, the specific
task's name as the needle) - this has a real substring-false-positive
footgun (a task named e.g. "Effect" would match inside "EffectManager"),
but grepping confirmed zero call sites anywhere in the file - dead code,
consistent with this session's "verify reachability before reporting"
standard. Not fixed, not worth it given it's unreachable.

## 2026-08-01 session (continued): verified both endianness-swap engines themselves are correct (not the source of the bugs above)

Quick confirmatory check after the `isInputsEqual`/`GameSettings` findings:
hand-verified all four `Utils::swapByteOrder` overloads in `utils.cpp`
(`bu16`, `bu32`, `float`, `u64`) against worked numeric examples - the
`bu16` shift-and-OR trick, the standard 2-stage bit-twiddling `bu32`
swap (verified `0x12345678` -> `0x78563412`), the `float` pointer-cast
reuse of the `bu32` algorithm, and the 3-stage `u64` mask/shift/OR
extension - all correct, standard, well-known constructions. Also
re-checked the Dolphin side's generic `swap_endian<T>` template
(`BrawlbackUtility.h`) - a union-based raw-byte-array `std::reverse_copy`,
correct for any POD size. Both swap engines check out; today's two big
findings were genuinely missing/incomplete call sites (a function never
calling any swap function, and a comparison never checking two of the
fields), not bad math in the primitives themselves.

## 2026-08-01 session (continued): scripted an exhaustive hook-type-vs-function-shape sweep of `InstallHooks()` - no new bugs, high confidence in the rest of the table

The `setFrameAdvanceCounter` (stack corruption) and `SkipDirectlyToCSS`
(wrong hook type) bugs from earlier this engagement were both found by
manually cross-referencing a hook's registered type (`syInlineHook*` =
auto-returns; `sySimpleHook*` = caller must manually resume via inline asm)
against its target function's actual shape. Rather than keep sampling pairs
by hand, wrote a small Python script (`/tmp/.../scratchpad`, not committed)
that regex-extracts every `api->sy*Hook*(...)` call in `InstallHooks()`
plus every function definition in the file, flags each function as
"naked" (has `__attribute__((naked))`) or "plain", and reports any hook
whose registered type doesn't match what its function's shape implies it
needs (naked-with-manual-`bctr`-resume hooked via an auto-returning
`syInlineHook*`, or a plain auto-returning function hooked via a
manual-resume `sySimpleHook*`).

Flagged 8 hits; all were already-accounted-for, not new bugs:
- `connectToAnybodyAsyncHook`, `netReportHook`/`2`/`3`/`4`, `netMinReportHook`
  - the regex doesn't understand `//` comments, and all 5 of these
    registrations are commented out (confirmed dead already). False
    positives from the script, not real findings.
- `setFrameAdvanceCounter` - the already-fixed stack-corruption bug. Its
  hook *type* (`sySimpleHook`) is actually correct - the function isn't
  `__attribute__((naked))` but still does a manual inline-asm `bctr` resume
  (my script's naked-detection only catches the attribute, not this
  pattern), which is exactly why the earlier fix was about the teardown
  *offsets* inside that inline asm, not the hook type itself.
- `beginningOfFrameLoop` - same "plain-but-manually-resumes-via-inline-asm"
  shape as `setFrameAdvanceCounter`. This is the *reference* case from the
  earlier fix (`setFrameAdvanceCounter`'s bug was copying this function's
  teardown offsets without adjusting for its own larger stack frame) - its
  own teardown is for its own frame and is correct.
- `BBBootTosqNetAnyOkiraku` - same shape again (plain function, manual
  inline-asm `bctr` resume with its own hand-written stack teardown). This
  was already independently checked and ruled out as a false lead in an
  earlier session (its own teardown offsets are correct for its own frame,
  by disassembly comparison, mentioned in the Errors and fixes history)

No new hook-type-vs-shape bugs found. This gives high confidence the rest
of the ~50-entry `InstallHooks()` table doesn't have more instances of the
`SkipDirectlyToCSS` class of bug, without needing to hand-verify every
remaining pair one at a time.

## 2026-08-01 session (continued): actually the bigger one - `GameSettings` received from the game was never endianness-swapped at all, corrupting the match's shared RNG seed

Immediately following the `isInputsEqual` fix, kept using the now-initialized
`brawlback-common` submodule to systematically check the OTHER shared wire
structs' consumer code for the same "field silently missing from the
handler" class of bug. Read `GameSettings.h`/`PlayerSettings.h`/
`BrawlbackControls.h`/`PlayerType.h` in full to enumerate every field wider
than 1 byte that would need an endianness swap crossing the PowerPC
(big-endian) <-> x86 host (little-endian) boundary: `GameSettings.stageID`
(`bu16`), `GameSettings.randomSeed` (`bu32`), and `PlayerSettings.nametag`
(`bu16[NAMETAG_SIZE]`, confirmed 2-byte elements, not the 1-byte
`displayName`/`connectCode` arrays right next to it). Everything else in
these three structs (`PlayerType : bu8`, all of `BrawlbackControls`'s
fields, `charID`/`charColor`/`colorFileIndex`/`controllerPort`/`rumble`) is
single-byte and needs no swap.

Then found the actual established convention by grepping both repos for
every endianness-swap call site that already exists: this codebase
consistently swaps each cross-endian EXI payload **exactly once, on
receipt**, symmetric in both directions -
`SwapPlayerFrameDataEndianness` (Dolphin receiving from the game, in
`handleLocalPadData`), `Util::FixFrameDataEndianness` (game receiving from
Dolphin, in `Rollback_Hooks.cpp`), `FixGameSettingsEndianness` (game
receiving `GameSettings` back from Dolphin via `CMD_SETUP_PLAYERS`, in
`CheckIsMatched`) - three confirmed instances of the same pattern. The
fourth leg of this same square - Dolphin receiving `GameSettings` *from*
the game via `CMD_START_MATCH` (`handleStartMatch`) - had no counterpart at
all: `std::memcpy(&gameSettings, payload, sizeof(GameSettings));` with
nothing after it. `fillOutGameSettings()` (`Rollback_Hooks.cpp`) populates
the struct with raw native (big-endian) values and `CMD_START_MATCH` sends
it as-is with no pre-swap either, confirming Dolphin's receiver was really
the one missing its half of the pair.

Traced the actual corruption: for the host,
`ProcessGameSettings`'s `isHost` branch never reassigns
`mergedGameSettings.randomSeed` - it round-trips game (0 swaps so far,
correct big-endian bytes) -> `handleStartMatch` (0 swaps, the bug - should
be 1) -> `CMD_SETUP_PLAYERS` -> the host's own game's
`FixGameSettingsEndianness` (1 swap) = **1 total swap** on a value that
needs an even count (0 or 2) to survive a round trip - an odd swap count
byte-reverses the value. Since `randomSeed` seeds the match's shared RNG
(read on the game side into `g_mtRandDefault.seed`/`g_mtRandOther.seed` per
`MergeGameSettingsIntoGame`), this is a real, live desync risk from the
very first frame of every hosted match (a byte-reversed seed is still
*some* deterministic seed, so it wouldn't crash - it would just mean both
players' RNGs could disagree if the seed isn't independently reconciled
elsewhere, which nothing in the code otherwise does). `stageID` has a
similar exposure via the opponent-echo path used for the non-host player's
stage selection.

**Fix** (commit `c1f91e0` on `savestates-efficiency-v2`, NOT pushed - no
fork exists yet): added `SwapGameSettingsEndianness()` to
`BrawlbackUtility.h`, swapping exactly the fields `FixGameSettingsEndianness`
already handles on the game side (`stageID`, `randomSeed`, `nametag[]`),
and called it in `handleStartMatch` right after the `memcpy`, matching the
pattern every other receiver in this codebase already follows.

This and the `isInputsEqual` fix above are, in my judgment, the two most
consequential fixes of this entire session - both are live, run on every
single match, and both were only found by actually reading the shared wire
structs field-by-field against their consumers rather than reasoning about
higher-level control flow, which is exactly what the newly-initialized
submodule made possible.

## 2026-08-01 session (continued): the big one - `isInputsEqual` silently omitted LTrigger/RTrigger, letting real mispredictions skip the rollback that should've fixed them

Found that the Dolphin fork's local clone here had never had its
`brawlback-common` submodule initialized (`git submodule status` showed a
leading `-` = uninitialized; the directory on disk was literally empty).
The pinned commit (`33d728c8d047374e63ec94bfd9eb11061c1f7953`) matched the
asm repo's submodule exactly though - so this was purely a local-clone gap,
not a real drift between the two repos. Ran `git submodule update --init`
to pull it, then `diff -rq`'d the two copies to confirm byte-for-byte
identical. This unlocked directly reading the actual wire-format structs
from the Dolphin side for the first time this session instead of only
inferring them from usage.

Read `BrawlbackPad.h`: 6 `bu32` button bitfields (`_buttons`, `buttons`,
`holdButtons`, `rapidFireButtons`, `releasedButtons`, `newPressedButtons`)
+ 8 `bs8` analog fields (`LAnalogue`, `RAnalogue`, `LTrigger`, `RTrigger`,
`stickX`, `stickY`, `cStickX`, `cStickY`) = 14 fields total. Cross-checked
against `isInputsEqual()` in `BrawlbackUtility.h` (already read earlier
this session, but not scrutinized field-by-field against the real struct
until now) - it only compares 12 of the 14: `LTrigger`/`RTrigger` are
silently missing from the `triggers` bool.

Verified `LTrigger`/`RTrigger` are real, independent input state, not
redundant with `LAnalogue`/`RAnalogue` - grepped `Rollback_Hooks.cpp`
(brawlback-asm) and confirmed they're captured from the real controller's
`_0x36`/`_0x37` bytes (distinct memory locations from
`m_lTriggerAnalog`/`m_rTriggerAnalog`, which feed `LAnalogue`/`RAnalogue`)
and written back into the live game pad struct on input injection
(`gamePad->_0x36 = pad.LTrigger;`) - so these fields can genuinely vary
independently of the analog trigger values and do affect real gameplay.

**Why this matters more than the other findings today**: `isInputsEqual`
is called from `CEXIBrawlback::updateSync()` (`EXIBrawlback.cpp:329`) -
this is the actual predicted-vs-actual resync check, the thing that decides
whether a rollback needs to happen at all. `updateSync` walks each
just-confirmed remote frame and calls `isInputsEqual(remoteInputs.pad,
predictedInputs.pad)` to see if the prediction the game already simulated
matches what the remote player actually did; a mismatch is what triggers
`IncrementalRB::Rollback(...)` and resimulation. Because `LTrigger`/`RTrigger`
were never compared, a misprediction limited to *just* those two fields
(remote player's L/R trigger digital state predicted wrong, everything else
right) would be silently reported as "inputs match, no rollback needed" -
the exact scenario rollback netcode exists to catch and correct, quietly
passing through uncorrected. This is a much higher-severity class of bug
than the other three fixes today (which affect timing precision, one byte
of restored memory, or only 3-4 player matches) since it's a direct hole in
the core correctness guarantee of the rollback system itself, live in every
single match regardless of player count.

**Fix** (commit `10219cd` on `savestates-efficiency-v2`, NOT pushed - no
fork exists yet): added `p1.LTrigger == p2.LTrigger && p1.RTrigger ==
p2.RTrigger` to `isInputsEqual`'s `triggers` bool in `BrawlbackUtility.h`.
Also fixed the same omission in `Match::isPlayerFrameDataEqual`
(`BrawlbackUtility.cpp`) - a near-identical but confirmed-dead-code
duplicate (the "TODO: this code is duplicated on the .cpp make it dry"
comment right above the live version was the tell) - for consistency in
case it's ever wired up.

## 2026-08-01 session (continued): more `brawlback-launcher` checks - `iniFile.ts`, `settingsManager.ts`, `verifyIso.ts`, `instance.ts` - no new bugs, one lead ruled out

Followed up the `geckoCode.ts` fix by reading `iniFile.ts` in full (the
`IniFile`/`Section` classes `geckoCode.ts` depends on). Confirmed `Section.getLines()`
already unconditionally drops blank (trimmed-empty) lines regardless of the
`removeComments` flag - meaning the blank-line half of the `geckoCode.ts`
filter fix was harmless-redundant, and the `#`-comment half was the actual
live bug, consistent with what was already fixed. `IniFile.save()`'s fix
comment (lines 191-196, "A section can legitimately have both raw lines...")
confirms that bug is intact from an earlier session. No new issues in this
file.

Checked a "confusing but not actually wrong" lead in `settingsManager.ts`:
`deleteMod(id: number)` treats `id` as a raw array index
(`modList.splice(id, 1)` gated by `id < modList.length`), which looked
suspicious next to `deleteConsoleConnection`/`editConsoleConnection`'s
proper `id`-field-based `findIndex` lookups. Checked `Mod`'s type
definition (`settings/types.ts`) - it has no `id` field at all (unlike
`StoredConnection`, which does); mods are genuinely referenced by array
index everywhere, matching `AppSettings.settings.selectedMod`'s own comment
("index of last played mod"). Traced the one real call site
(`renderer/lib/hooks/useMods.ts` line 33) - passes `index` explicitly, named
consistently all the way down. Not a bug, just a parameter that's misleadingly
named `id` when it's really an index - ruled out.

Read `verifyIso.ts` (MD5/SHA1 ISO hash verification against the known-good/
known-bad hash tables) and `dolphin/instance.ts` (Dolphin process spawn
wrapper) - both clean. Noted `verifyIso.ts`'s `stream.on("readable", ...)`
handlers only call `.read()` once per event instead of looping until it
returns null (the Node-recommended pattern) - a real anti-pattern, but
this code is explicitly inherited from Slippi's original (working, shipped)
implementation per its own comments, so treating it as new/actionable
without a way to build and exercise the actual Electron app here would be
guessing; left alone.

The launcher's `src/` tree is ~200 files; this and the `geckoCode.ts` fix
above cover a meaningful but partial slice of it (config/ini parsing,
settings persistence, ISO verification, Dolphin process spawning). Plenty
more surface remains (`broadcast/`, `console/` mirroring, `replays/`
parsing, the whole `renderer/` React tree) for a future pass.

## 2026-08-01 session (continued): pivoted to `brawlback-launcher` - found a real no-op filter bug in `loadGeckoCodes`

The Dolphin fork's `Brawlback/` directory is now fully audited (every
`.cpp`/`.h` in it read at least once across this and prior sessions, 3 real
bugs found and fixed just this pass). Pivoted to `/workspace/brawlback-launcher`
(branch `master`, 20 commits already ahead locally from earlier sessions -
install pipeline, download logic, INI parsing, deep links, menu links, etc.
- none of that touched this pass, still needs pushing once
`Brawlback-Team/brawlback-launcher` gets forked).

Checked `playkey.ts` first since it's the direct connective tissue to the
`lylat.json` reading in `CEXIBrawlback::getUserInfo()` audited on the
Dolphin side earlier this session - it already has detailed, accurate
cross-referencing comments (matches the exact `getExeDirectory()`/
`GetUserPath(D_USER_IDX)` logic verified in `EXIBrawlback.cpp`), confirming
this file was already properly fixed/verified in an earlier pass. Not
touched further.

Found a new bug in `src/dolphin/config/geckoCode.ts`'s `loadGeckoCodes`:

```ts
const lines: string[] = ini.getLines("Gecko", false).filter((line) => {
  return line.length !== 0 || line[0] !== "#";
});
```

Intent is obviously to drop blank lines and `#`-prefixed comment lines
before parsing the `[Gecko]` ini section into code blocks. `||` was used
where `&&` was needed. Verified with a standalone `node -e` repro (not just
reasoning about it) that `line.length !== 0 || line[0] !== "#"` evaluates to
`true` for an empty string, a `"#comment"` line, and a normal line alike -
because an empty string's `line[0]` is `undefined`, and `undefined !== "#"`
is `true`, so the OR always finds a true operand no matter what. The filter
callback can never return `false` - it's a complete no-op.

Effect: blank lines and comment lines from the `[Gecko]` section were never
actually being dropped. They fell through into the `lines.forEach` parsing
loop's `switch (line[0])`, hit the `default:` branch (since only `$` and
`*` are handled explicitly), and got pushed into `gcode.codeLines` for
whatever gecko code entry was currently being built - an empty string entry
for every blank line, and raw comment text mistaken for real gecko-code hex
data for every `#` line. Both corrupt the parsed `codeLines` array for any
code block in the ini that's followed by either, before those codes are
ever re-serialized or otherwise used.

**Fix** (commit `d463138` on `master`, NOT pushed yet - no fork exists for
`Brawlback-Team/brawlback-launcher`): changed `||` to `&&`. `husky`'s
pre-commit lint/format hooks passed cleanly; also ran `npx tsc --noEmit`
across the whole project and confirmed no new type errors introduced by
this file.

## 2026-08-01 session (continued): rest of `EXIBrawlback.cpp` threading/matchmaking/net-receive code + `Matchmaking.h` - no new findings

Read through the remaining not-yet-explicitly-covered functions in
`EXIBrawlback.cpp`: `ProcessFrameAck`, `ProcessGameSettings`,
`ProcessNetReceive`, `NetplayThreadFunc`, `MatchmakingThreadFunc`,
`connectToOpponent`, `handleFindMatch` (including the issue-#72
direct-connect payload parsing I wrote earlier this engagement -
`search.mode = payload[0]`, 18-byte Shift-JIS connect code at `payload[1..18]`,
matches what `Rollback_Hooks.cpp`'s `setNextAnyOkirakuCaseFive` sends),
`handleStartMatch`, `handleEndMatch` (the `lylat.gg/reports` CURL POST -
all fields going into the JSON are numeric, no injection risk from
untrusted string data), plus `Matchmaking.h`'s full class declaration.

No new bugs found. Re-confirmed the already-documented thread-lifecycle
risk is visible directly in `MatchmakingThreadFunc`'s loop (`while
(this->matchmaking) { switch (state) { case CONNECTION_SUCCESS: break; ... }
}` - once state reaches `CONNECTION_SUCCESS` this is a genuine tight
busy-spin with no sleep/yield, burning a full CPU core until the match ends
or matchmaking is torn down - consistent with, not a new addition to, the
2026-07-31 writeup). Also noticed `Matchmaking.h`'s `int m_isSwapAttempt =
false;` is write-only (one assignment in `Matchmaking.cpp` line 601, zero
reads anywhere) - vestigial, not a bug, not touched.

## 2026-08-01 session (continued): `isPredicting` was a single shared scalar instead of per-player - breaks 3-4 player matches only

Continued into `EXIBrawlback.h`/`.cpp` (re-reading with fresh eyes past
what was already covered - the thread-reassignment bug documented in the
previous 2026-07-31 section is still unfixed, still needs careful design,
not touched this pass).

Found: `bool isPredicting;` (`EXIBrawlback.h` line 126, no initializer) is
written in `CEXIBrawlback::getRemoteInputs(bu32& locFrame, u8 playerIdx,
bool& skipFrame)` - `isPredicting = false;` when real remote inputs were
found for this player this frame, `isPredicting = true;` when falling back
to predicted (repeated-previous) inputs for this player this frame - and
read in `CEXIBrawlback::updateSync(bu32& locFrame, bu8 playerIdx)` to decide
whether to run the predicted-vs-actual resync check for that player.

The call structure: `getRemoteInputs` is called from the DMA frame-data-request
handler in a loop over every non-local player index (`for (s32 i = 0; i <
this->numPlayers; i++) { ... getRemoteInputs(currentFrame, i, ...); }`,
around line 251), each call overwriting the single shared `isPredicting`.
`updateSync` is called later from a *separate* DMA command
(`handleUpdateSync`, its own loop over `i in 0..numPlayers`, line ~1403-1409).

In a 2-player match this is harmless by coincidence - there's only ever one
non-local remote player, so the single scalar really does describe "the"
remote player's state. In a 3-4 player match (this game supports up to
`MAX_NUM_PLAYERS`), `getRemoteInputs` runs for *multiple* remote players
per frame before `updateSync` runs for any of them, so by the time
`updateSync` executes for player 0, `isPredicting` has already been
overwritten by whatever happened for player 1/2/3's `getRemoteInputs` call
later in that same frame's loop. Every player's resync check ends up using
the *last-processed* remote player's prediction state instead of its own -
meaning the "did remote inputs match our prediction" logic that decides
whether to actually roll back can silently use the wrong player's data,
either skipping a needed rollback or triggering the resync-check logic
using irrelevant data for everyone except whichever player happened to be
last in the loop.

**Fix** (commit `d9b2e3c` on `savestates-efficiency-v2`, NOT pushed):
changed the declaration to `bool isPredicting[MAX_NUM_PLAYERS] = {};` and
indexed all three usages by the already-in-scope `playerIdx` parameter at
each site (both writes in `getRemoteInputs`, the one read in `updateSync`).
Mechanical, low-risk change - `playerIdx` was already a parameter in both
functions, and `predictedInputs.playerFrameDatas[playerIdx]` right next to
the read site was already correctly indexed, just this one flag wasn't.

This only matters for 3+ player matches - given the project's current focus
is 1v1 ranked/unranked, this is lower urgency than the RollbackSavestate or
TimeSync fixes above, but still a real, confirmed correctness bug worth
having fixed before FFA/doubles rollback netplay is exercised.

## 2026-08-01 session (continued): integer-division bug in `TimeSync.h`'s `MS_IN_FRAME`/`USEC_IN_FRAME`

While reading the remaining unaudited headers (`incremental_rb.h`,
`tiny_arena.h`, `Netplay.h`, `BrawlbackUtility.h` - all clean, nothing new;
confirmed `Brawlback::Clamp(T input, T Max, T Min)` in `BrawlbackUtility.h`
has zero call sites anywhere, unlike the differently-signatured, actually-used
`::Clamp(T& value, const T& low, const T& high)` in `util.h` - dead code,
not touched), found this in `TimeSync.h`:

```cpp
constexpr float MS_IN_FRAME = 1000 / 60;
constexpr s32 USEC_IN_FRAME = MS_IN_FRAME * 1000;
```

`1000` and `60` are both `int` literals, so `1000 / 60` is evaluated as
**integer division** (`= 16`) before the result is ever converted to
`float` for the assignment - the fractional part (`.667`) is gone before
`MS_IN_FRAME` even exists. This doesn't need empirical verification like the
boost::icl finding above; it's unambiguous C++ operator/conversion
semantics, provable by inspection alone. `USEC_IN_FRAME` then comes out to
`16 * 1000 = 16000`, when the correct value (matching the actual ~59.94-60fps
frame rate this whole netcode is built around) is `1000000 / 60 ≈ 16667`.
That's a systematic **~4% low bias** baked into a compile-time constant used
in two live spots in `TimeSync.cpp`:

- line 70, `TimeSync::TimeSyncUpdate`: `this->framesToSkip = ((offsetUs -
  TIMESYNC_MAX_US_OFFSET) / USEC_IN_FRAME) + 1;` - dividing by a
  ~4%-too-small frame duration overestimates how many frames need to be
  skipped to catch up, sometimes by a whole extra frame depending on the
  offset magnitude (e.g. a real 160ms offset: correct math skips 9 frames,
  buggy math skips 10).
- line 157, `TimeSync::ProcessFrameAck`: `s64 frameDiffOffsetUs =
  USEC_IN_FRAME * (timing.frame - frame);` - converts a frame-count
  difference into a microsecond time offset for ack/ping tracking, with the
  same ~4% low bias baked in every time.

Neither of these individually crashes anything, but both feed the core
time-sync pacing logic this whole rollback system depends on to decide when
to stall/skip frames relative to the remote peer - a small systematic bias
compounding across an entire match is exactly the kind of thing that would
show up as "the netcode feels slightly off / drifts over a long match" in
practice without an obvious root cause, unless someone happened to notice
this one three-line constant.

**Fix** (commit `5830a0b` on `savestates-efficiency-v2`, NOT pushed - no
Dolphin fork exists yet): changed to `constexpr float MS_IN_FRAME = 1000.0f
/ 60;`, forcing float division. `USEC_IN_FRAME` now correctly evaluates to
`16666` (int-truncated from `16666.67f`).

## 2026-08-01 session: confirmed off-by-one data-corruption bug in `RollbackSavestate` via standalone boost::icl test programs

Continued auditing `incremental_rb.cpp` (the live `IncrementalRB` engine) with
fresh eyes, specifically the non-`MULTITHREAD` (live) path of
`RollbackSavestate()` which uses `boost::icl::interval_set<uintptr_t>` to
compute `changedSet - excludeSet` and walk the resulting fragments to copy
each fragment's "before" snapshot data back over live game memory.

This function builds each tracked page's interval as
`discrete_interval<uintptr_t>::closed(pageStart, pageStart + pageSize)` -
deliberately one byte "too long" so that two adjacent tracked pages'
intervals share exactly one point and get auto-merged by boost::icl into a
single continuous run. After subtracting `excludeSet`, the code walks the
resulting fragments and has two branches depending on whether the fragment's
lower bound is "contained" (`boost::icl::contains(*it, it->lower())`):

- **if-branch** (lower bound is real, unclipped data start): `orig_ptr =
  it->lower()` directly, `size = it->upper() - it->lower()`, then had a
  conditional `if (!contains(*it, it->upper())) size--;`.
- **else-branch** (lower bound was clipped by excludeSet, i.e. `it->lower()`
  is itself an excluded/boundary point): `orig_ptr = it->lower() + 1` (to
  skip past the excluded point), `size = it->upper() - it->lower() - 1`,
  then the same conditional extra decrement if the upper bound is also
  clipped.

I was suspicious the else-branch's `+1`/`-1` looked asymmetric against the
if-branch's lack of any base adjustment, but reasoning abstractly about
`boost::icl`'s dynamic-bounds semantics wasn't enough to be confident either
way. Rather than guess, I wrote three small standalone C++ programs
(`/tmp/.../scratchpad/icl_test{,2,3,4}.cpp`) that `#include` the actual vendored
boost::icl headers from this repo (`Source/Core/Core/Brawlback/include/boost/icl/`)
and compiled/ran them with plain `g++ -std=c++17` to empirically observe real
`interval_set<uintptr_t>` behavior for:
1. two adjacent closed-page intervals merging into one run (confirms the
   merge trick works, and that `.upper()-.lower()` on the unclipped merged
   run already equals the *correct* total byte count - `2 * pageSize` for a
   2-page run, no further +1/-1 needed).
2. a single isolated full page (confirms raw width == `pageSize` exactly,
   unclipped).
3. excluding a chunk from the middle of a single page (produces two
   fragments; hand-verified the correct byte count for each against the
   fragment boundaries and bound-containment flags).
4. excluding a chunk near the end of a merged 2-page run (the case that
   actually nails it down): the first (if-branch) fragment came back as
   `[0x1000, 0x2900]`, `contains_lower=1`, `contains_upper=0`, raw width
   `6400`. Hand-computed correct byte count for real data `[0x1000, 0x28FF]`
   inclusive is `0x28FF - 0x1000 + 1 = 6400` - i.e. **the raw width is
   already exactly correct, with no decrement needed**, even though
   `contains_upper` is false and would trigger the old conditional
   decrement.

So the if-branch's `if (!contains(*it, it->upper())) size--;` was live,
reachable, and simply **wrong** - it doesn't belong there at all (that
decrement's rationale genuinely applies only to the else-branch, which
already has its own separate `-1` baseline to compensate for the lower
bound being clipped; the if-branch's lower bound needs no such compensation,
so its raw width is already correct in every case I tested, both clipped and
unclipped at the upper edge).

**Effect of the bug**: every time `RollbackSavestate` had to restore a
tracked-page run whose lower edge sits at a genuine already-tracked page
boundary (the common case) *and* whose upper edge got clipped by
`excludeSet` (e.g. adjacent to an excluded heap/stack region, or ending
mid-page from any exclusion) - which is a realistic, non-contrived
occurrence in real gameplay given the arena/heap exclusion ranges - the
restore copied exactly one byte less than it should have. The very last byte
of that fragment silently kept its post-rollback (i.e. wrong, "future")
value instead of being restored to the correct pre-rollback historical
value. This is a single stray byte per occurrence, which would manifest (if
it manifests at all detectably) as sporadic, hard-to-reproduce desyncs
between netplay peers with no obvious associated crash or log message -
exactly the kind of bug that's very hard to find by symptom-chasing but
straightforward to find by systematically re-deriving the actual space of
addresses page-tracking is supposed to cover.

**Fix** (commit `4d6d7d6` on `savestates-efficiency-v2`, NOT pushed - no
fork exists yet for `Brawlback-Team/dolphin`): removed the erroneous
conditional decrement from the if-branch entirely, leaving `size = it->upper()
- it->lower()` unconditional there. Left the else-branch untouched (its
decrement logic was independently confirmed correct via the same tests -
scenario 3/4's second fragment, e.g. `[0x1600,0x2000)` with `raw=2560`,
`size = 2560-1 = 2559` matching the hand-computed correct answer for that
clipped-at-both-ends-of-original-page fragment exactly).

This is now build-verified only insofar as the standalone test programs
compiled and ran cleanly against the real vendored boost::icl headers (not
the full Dolphin build, which remains impractical in this environment) -
the actual code-path integration was re-read carefully post-edit to confirm
no unintended change to control flow, variable scope, or the `ssData`/`size`
computation surrounding it.

The three previously-fixed bugs in this same file/area from an earlier
session (arena leak in `Memmap.cpp`, off-by-one in `mem.cpp`'s
`GetWrittenPages`, orphaned-allocation fix in `OnPagesWritten`) were
re-confirmed intact and untouched by this pass - I re-read `OnPagesWritten`
in full and its fix comment (lines 587-596) is still there verbatim.

Also used this pass to re-verify `Rollback(s32 currentFrame, s32
rollbackFrame)`'s index arithmetic (the commented-out asserts around it)
by hand-tracing the worked example already in the file's own comments
(frame 15 rolling back to frame 10) plus the single-frame-rollback edge
case (rollback exactly 1 frame, the common case given `FRAME_DELAY=1`).
Both check out as correct. In particular, the disabled `assert(...
endingSavestateIdx != currentSavestateIdx)` at line 531 would actually have
fired (incorrectly) on every ordinary single-frame rollback - that's
*why* it's disabled, not evidence of a bug in the actual rollback logic,
which I confirmed by hand-tracing produces the correct result (exactly one
`RollbackSavestate` application) in that case. Not a new finding, just
ruling out a lead so it doesn't get re-investigated later.

Next: continue auditing remaining unread portions of the Dolphin fork
(anything in `Brawlback/` or `HW/EXI/EXIBrawlback.*` not yet fully covered)
and keep applying the same "don't trust it until you've empirically or
concretely verified it" standard. Per the user's standing instruction, not
stopping or asking whether to continue - moving straight to the next file.

## 2026-07-31 session (continued): a likely severe crash bug - thread reassignment on a 2nd match, not fixed (needs careful design, not a blind patch)

Followed up on the thread-safety angle noted at the end of the previous
section. Found something that looks like a genuinely severe, well-reasoned
crash risk, but is NOT fixed here - the fix needs multi-part thread-exit
signaling I can't safely design and verify without building/live-testing,
so this is written up in full instead of patched blind.

**The setup:** `CEXIBrawlback` (`EXIBrawlback.h`) is a single, session-long
object - confirmed earlier this session (constructed once in its own
constructor, not per-match). It owns exactly one `std::thread netplay_thread`
member and one `std::thread matchmaking_thread` member. Both get
**re-assigned** (not constructed fresh) on every new matchmaking attempt:
`this->matchmaking_thread = std::thread(&CEXIBrawlback::MatchmakingThreadFunc, this);`
(in `handleFindMatch`) and `this->netplay_thread = std::thread(&CEXIBrawlback::NetplayThreadFunc, this);`
(in `connectToOpponent()`, called from inside `MatchmakingThreadFunc`).

**The problem:** per the C++ standard, `std::thread::operator=(std::thread&&)`
calls `std::terminate()` (aborts the whole process) if the target already
represents a joinable thread - and "joinable" just means "not yet `.join()`'d
or `.detach()`'d", regardless of whether the underlying thread has already
finished running. Grepped every `.join()`/`.detach()`/`.joinable()` call in
this file: **`netplay_thread` is only ever joined in the destructor.**
`matchmaking_thread` is joined in the destructor and in
`handleCancelMatchmaking()` (an explicit user-cancel path) - but not
anywhere on normal match completion.

**Why this matters even though matches "work" today:** `MatchmakingThreadFunc`'s
own loop (`while (this->matchmaking) { switch(state) { case CONNECTION_SUCCESS: break; ... } }`)
has no sleep and no exit condition once it reaches `CONNECTION_SUCCESS` -
it busy-spins on that state for the rest of the object's lifetime (i.e.
the whole Dolphin session) unless `this->matchmaking` itself becomes null,
which nothing does. `NetplayThreadFunc`'s loop only exits on an actual ENet
disconnect event or a stalled connection - not on the game/match simply
ending while the peer connection is still healthy. Neither Dolphin-side
match-end logic (previously, nothing at all - `Netplay::EndMatch()` on the
ASM side is purely local cleanup and sends nothing to Dolphin) resets
these threads. **This means: if a player returns to the menu after a match
and queues for a second match in the same Dolphin session, both
`handleFindMatch`'s `matchmaking_thread` reassignment and (once matched)
`connectToOpponent()`'s `netplay_thread` reassignment would very likely hit
a still-joinable (often still-actively-running) thread object, triggering
`std::terminate()` and crashing the entire Dolphin process.**

This may well be currently unreachable in practice for a mundane reason:
per the punch list at the top of this doc, the whole "return to CSS and
requeue" menu flow is itself still WIP/prototype-stage (issue #72 and
friends), so it's plausible nobody has actually been able to trigger a
real second-match-in-one-session scenario yet to discover this crash. That
would explain why this hasn't been reported as a known crash despite how
severe and unconditional it looks in the code.

**Why not fixed directly:** a correct fix needs to make BOTH threads
actually exit their loops before the next match's reassignment happens,
which means solving two different problems that interact:
1. `NetplayThreadFunc` needs an explicit "please stop" signal decoupled
   from actual ENet disconnection (e.g. a dedicated `shouldStop` flag
   checked in its loop condition, set at match-end).
2. `MatchmakingThreadFunc`'s infinite busy-spin in `CONNECTION_SUCCESS`
   needs an actual exit condition (right now the only way out is
   `this->matchmaking` becoming null, which nothing does and would need
   its own careful handling given other code may depend on `matchmaking`
   staying valid).
3. Whatever join() gets added needs to run from a thread that isn't
   `netplay_thread`/`matchmaking_thread` themselves (join-from-self is UB/
   deadlock) - `handleEndMatch` (a `DMAWrite` handler, so it runs on the
   CPU/EXI thread) is the natural place now that issue #76's fix earlier
   this session means `CMD_MATCH_END` actually gets sent and `handleEndMatch`
   actually gets called at the end of every match. But joining there
   *before* the loops above have an actual exit condition would just
   deadlock/freeze Dolphin instead of crashing it - arguably worse, since a
   crash at least ends the freeze.

Given the real risk of trading a "sometimes crashes on 2nd match" bug for
a "sometimes silently freezes the game" bug if this is designed carelessly
and can't be build/live-tested to confirm, this needs a deliberate design
pass (ideally by someone who can actually run a two-match session and
observe it), not a blind patch. Documented in full here so whoever picks
it up doesn't need to re-derive any of this reasoning - the fix, in broad
strokes, is: add a `shouldStop`-style flag both thread loops check, set it
and signal both threads to wake up (`enet_host_service`/loop conditions)
at the start of `handleEndMatch`, `join()` both *after* confirming they've
actually exited, then proceed with the rest of `handleEndMatch`'s existing
report-sending logic.

## 2026-07-31 session (continued): finished EXIBrawlback.cpp - the rest is confirmed dead

Read the remaining handlers: `handleDumpAll`, `handleAlloc`, `handleDealloc`,
`handleFrameCounterLoc`, `handleStartReplaysStruct`, `handleReplaysStruct`,
`handleEndOfReplay`. Before bug-hunting in them, checked reachability from
the ASM side the same way as the earlier `NetReport`/`SkipDirectlyToCSS`
checks: all four of `CMD_SEND_ALLOCS`/`CMD_SEND_DEALLOCS`/`CMD_SEND_DUMPALL`'s
only ASM-side senders (`Match::alloc_gfMemoryPool_hook`/`free_gfMemoryPool_hook`/
`allocGfMemoryPoolEndHook`/`dump_gfMemoryPool_hook`) are exactly the four
hooks already confirmed commented-out in `brawlback-asm`'s `InstallHooks()`
(the memory-heap-tracking hooks, `Match::dump_gfMemoryPool_hook` etc.) -
and grepped `Rollback_Hooks.cpp` for any sender of `CMD_REPLAY_START_REPLAYS_STRUCT`/
`CMD_REPLAY_REPLAYS_STRUCT`/`CMD_REPLAYS_REPLAYS_END` and found zero, not
even commented out. So this entire back half of `EXIBrawlback.cpp` -
`dynamicRegions` heap-allocation tracking (the pre-`IncrementalRB` approach)
and the replay-recording pipeline (`curReplayJson`, `.brba` file writing) -
is unreachable from either side of the wire. Confirms and extends the
already-known "replay format/recording isn't wired up anywhere" finding
from the launcher-side audit earlier this session, now from the Dolphin
side too: there is genuinely no live replay-recording code path at all,
not just an unimplemented launcher-side viewer.

**`EXIBrawlback.cpp` is now fully covered** - every function either read
and verified correct, or confirmed unreachable via the same
grep-for-senders technique used successfully earlier in this session.

Moving next to a thread-safety pass across the netplay/matchmaking/EXI
threading (`NetplayThreadFunc`, `MatchmakeThread`, EXI DMA callbacks all
run on different threads and share state like `gameSettings`/`numPlayers`)
- a promising area given how concurrency-heavy this design is and how
successful the static-analysis approach has been so far.

## 2026-07-31 session (continued): EXIBrawlback.cpp deep pass - one more real bug

Started the promised full pass on `EXIBrawlback.cpp` (1544 lines - the
biggest single file in the Brawlback area, only partially covered by
earlier sessions for specific things). Read through the constructor,
`handleCaptureSavestate`/`handleLoadSavestate`, `handleLocalPadData`,
`handleFrameDataRequest`, `getLocalInputs`/`getRemoteInputs`, `updateSync`,
`shouldRollback`, `isRollbackMode`, `storeLocalInputs`, `handleSendInputs`,
`ProcessIndividualRemoteFrameData`, `ProcessRemoteFrameData`,
`GetLatestRemoteFrame`, `DMAWrite`/`DMARead`, `handleFrameAdvanceRequest`.

**One real bug found and fixed (commit `a922d71` in the local Dolphin
clone):** `storeLocalInputs` trimmed the local player's input queue with
`if (size() > FRAMEDATA_MAX_QUEUE_SIZE) pop_front()` checked *before*
pushing the new element, not after. When the queue was already exactly at
the 15-element cap, that check was false (15 is not > 15), so nothing
popped, and the push grew it to 16 - one over the intended max - every
other call, oscillating between 15 and 16 instead of holding a hard cap.
`ProcessIndividualRemoteFrameData`, elsewhere in the same file, handles the
equivalent trim for each *remote* player's queue correctly (push first,
then `while (size() > MAX) pop_front()` afterward) - changed
`storeLocalInputs` to match that same convention for both correctness and
internal consistency. Minor severity (off by one element in a 15-slot
buffer) compared to the `TimeSync.cpp` bugs, but real and cheap to fix.

**One near-miss, self-corrected before reporting:** `handleFrameDataRequest`
has an inner `if (this->framesToAdvance == 0) { use blank inputs; continue; }`
check inside a loop that's already gated by an outer `if (framesToAdvance != 0)`
- looked like unreachable/dead code at first glance. Traced further before
concluding that: `getRemoteInputs` (called earlier in the same loop
iteration, for other player indices) can itself set
`this->framesToAdvance = 0` as a side effect (its non-rollback/delay-based
branch, when no remote frame data is available yet). So in a 3-4 player
match, if an earlier remote player's call stalls the frame, the inner
check correctly catches later remote players in the *same* loop pass. Not
a bug - just needed tracing the actual side effects of a called function
before jumping to "this looks redundant."

**Everything else read in this pass is clean and correctly matches the
rollback pseudocode comment it links to** (the `updateSync`/`shouldRollback`
logic references
`https://gist.github.com/rcmagic/f8d76bca32b5609e85ab156db38387e9` and
follows it faithfully as far as can be checked without live-testing).

Not build-verified (same reason as the `TimeSync.cpp` fixes - no practical
full Dolphin build in this environment) but low syntactic risk (a `while`
loop moved after an existing `push_back`, no new types or APIs).

Remaining unread in `EXIBrawlback.cpp`: `handleDumpAll`/`handleAlloc`/
`handleDealloc`/`handleFrameCounterLoc`/`handleReplaysStruct`/
`handleStartReplaysStruct`/`handleEndOfReplay` (the replay/memory-dump
tooling, lower priority - replays aren't even wired up on the launcher
side yet per earlier findings), and `TransferByte`/`IsPresent` (already
skimmed, trivial overrides). Next continuation point if picking this
file back up.

## 2026-07-31 session (continued): swept the rest of the Dolphin fork's Brawlback/ directory

Continued straight through per standing instruction (never suggest
stopping). After the `TimeSync.cpp` fixes, read every other file directly
under `Source/Core/Core/Brawlback/` not yet covered this session:

- **`Netplay.cpp`/`.h`** - packet construction/broadcasting. Clean, no
  issues.
- **`Matchmaking.cpp`** (766 lines) - read in full. `getMMHostForSearchMode()`
  always calls `getMexMMHost()` (the "isMexMode" branch is commented out),
  and both `MM_HOST_DEV` and `MM_HOST_PROD` are literally the same string
  (`"lylat.gg"`) in `Matchmaking.h` - so this always resolves to the real,
  correct backend regardless, but it does mean a user's
  `m_slippiCustomMMServerURL` setting (a real, Slippi-inherited Dolphin
  config option) is silently ignored if anyone ever tries to use it. Not
  fixed - moot given there's only one real Brawlback matchmaking backend
  right now, but worth knowing if custom-server support is ever wanted.
  Cross-checked `m_localPlayerIndex` assignment/comparison logic against
  Ishiiruka's `SlippiMatchmaking.cpp` - identical, confirmed correct.
- **`BrawlbackUtility.cpp`** - clean. Re-confirmed (via fresh grep, not
  just trusting the old note) that `isButtonPressed` (ignores its own
  `button` parameter, always checks for Z) and `isPlayerFrameDataEqual`
  are still both dead code with zero call sites - matches the
  already-established finding from an earlier session, not newly
  re-discovered by accident. Also found `Mem::isIntersect`/`removeInterval`/
  `manipulate2` (custom interval-arithmetic for a `PreserveBlock` list) -
  `isIntersect`'s formula only checks one direction of overlap, but its
  only caller (`removeInterval`, also dead) always swaps operands first so
  the one-directional check happens to be sufficient in that context
  anyway. All three functions: zero call sites anywhere in the fork.
- **`Savestate.cpp`** (`BrawlbackSavestate` class) and **`SlippiUtility.cpp`**
  (entire file, including `SlippiInitBackupLocations` and the `Mem::`
  byte-buffer helpers) - confirmed **zero call sites for any of it**,
  anywhere. This is the dead pre-`IncrementalRB` savestate approach
  mentioned in earlier sessions' notes - now confirmed exhaustively rather
  than assumed.
- **`include/incremental-rollback/tiny_arena.cpp`** - the actual bump
  allocator backing the *live* `IncrementalRB` system. `arena_init`/
  `arena_alloc`/`arena_clear` all correct. `arena_pop_latest` has a real
  bug (compares against `arena->offset`, i.e. one-past-the-end of the most
  recent allocation, where it should compare against `arena->prev_offset`,
  the actual start address of that allocation) - but confirmed via grep
  that `arena_pop_latest` itself has zero call sites anywhere, so this bug
  is currently inert. Not fixed, noted for if this function ever gets used.

**This completes a full audit of every file directly under
`Source/Core/Core/Brawlback/`** in the Dolphin fork (excluding
`include/incremental-rollback/incremental_rb.cpp`, `mem.cpp`, and
`job_system.cpp`, which were already thoroughly covered in earlier
sessions - the arena leak/off-by-one/orphaned-allocation fixes, and
`job_system.cpp`'s confirmed-inert `MULTITHREAD`-gated worker threads).
Net result this stretch: the two real `TimeSync.cpp` fixes above, and
several additional "confirmed dead, matches or extends prior findings, not
touched" landmines - no new live bugs found beyond `TimeSync.cpp`, but
significantly higher confidence now that the *whole* directory has
actually been read, not just the parts a previous session happened to
already dig into for other reasons.

Next: `EXIBrawlback.cpp`/`.h` (the biggest single file in this area,
partially audited across earlier sessions for specific things -
`ProcessGameSettings`, `handleFindMatch`, `handleStartMatch`,
`handleEndMatch` - but not given a full line-by-line pass with the
Ishiiruka-comparison technique that just found the `TimeSync.cpp` bugs).

## 2026-07-31 session (continued): the biggest bug this session, found in the Dolphin fork's TimeSync.cpp

User said to never suggest stopping/holding, keep working straight through
- redirected from the raw-asm coverage wall to auditing the Dolphin fork's
C++ (`/workspace/brawlback-team-dolphin`), specifically `Netplay.cpp` and
`TimeSync.cpp`, which hadn't had a deep pass yet. `Netplay.cpp` (packet
construction/broadcasting) - read in full, clean, no issues.

**`TimeSync.cpp` - found a real, significant bug, verified by direct
comparison against the actual Slippi source it says it was ported from**
(the file's own top comment: "pretty much all of this time sync stuff was
taken from slippi"). `Ishiiruka` (Slippi's Dolphin fork, already in this
workspace) has the equivalent logic in `Source/Core/Core/Slippi/SlippiNetplay.cpp`.

1. **The circular buffer wraparound was broken** (commit `01a5731` in the
   local Dolphin clone). `frameOffsetData[i].idx` (the write cursor for a
   30-slot circular buffer of frame-timing-offset samples, used by
   `calcTimeOffsetUs()`'s trimmed-mean to decide whether to skip a frame
   for time sync) was advanced via
   `(idx + 1) & ONLINE_LOCKSTEP_INTERVAL` - bitwise AND. `ONLINE_LOCKSTEP_INTERVAL`
   is `30` (confirmed identical in both `brawlback-common/BrawlbackConstants.h`
   and Ishiiruka's `SLIPPI_ONLINE_LOCKSTEP_INTERVAL`). A bitwise-AND mask
   only correctly wraps an incrementing index when the interval is
   `(power of 2) - 1` (e.g. 31, 15, 7) - 30 is not one of those. Checked
   Ishiiruka's real line for the equivalent: `(frameOffsetData[pIdx].idx + 1) % SLIPPI_ONLINE_LOCKSTEP_INTERVAL`
   - modulo, not AND. Verified computationally (ran the actual recurrence)
   that `(idx + 1) & 30` collapses to a fixed point almost immediately
   regardless of starting value, since bit 0 is always cleared by the mask
   - meaning this "circular" buffer never actually rotated through its 30
   slots after first filling up; most samples the time-sync average was
   computed from went permanently stale. This is core, always-active
   netcode logic (runs continuously during every match to decide
   frame-skip timing), not an edge case - very plausibly a real
   contributor to degraded sync quality in every match ever played on this
   fork.
2. **`TimeSync::startGame()` never reset `frameOffsetData` at all**, unlike
   `lastFrameTimings`/`lastFrameAcked`/`ackTimers` right next to it in the
   same loop. Traced where `TimeSync` itself gets constructed
   (`CEXIBrawlback`'s own constructor, `this->timeSync = std::make_unique<TimeSync>();`,
   confirmed via the `"BRAWLBACK exi ctor"` log line right above it - runs
   once per Dolphin session, not per match) versus where `startGame()`
   gets called (once per match) - confirming `startGame()` is the *only*
   per-match reset point for this object, and it was missing this one
   field. Checked Ishiiruka's equivalent per-connection init
   (`SlippiNetplayClient`'s constructor) and confirmed it does reset
   `frameOffsetData[i]` there. Without this, playing a second match in the
   same Dolphin session would carry stale offset samples (and, after fix
   1, a stale `idx`) over from the previous match. Fixed by adding
   `this->frameOffsetData[i] = FrameOffsetData();` to the same loop.

**Not build-verified** - a full Dolphin build isn't practical in this
environment (huge codebase, no CMake config already set up, would need
many system dependencies and a long build time on a 4-core box). Carefully
re-read the diff for type/syntax correctness instead (`FrameOffsetData` is
a plain aggregate struct with no user constructor, so
`frameOffsetData[i] = FrameOffsetData();` is valid value-initialization +
assignment - nothing exotic). **Not live-tested either.** Committed
locally in `/workspace/brawlback-team-dolphin` same as the rest of this
session's Dolphin-fork work - still blocked on the same "no fork exists
yet" issue as everything else there. Given the "runs every frame of every
match, directly ported from working Slippi code, and the divergence from
the original is a single-character operator change" profile of this bug,
this is probably the single most confidently-real bug found this entire
session, even without a build/live-test to nail it down completely.

## 2026-07-31 session (continued): pushed further into decompiled-source verification, hit the real coverage wall

User asked to keep pushing into the Ghidra-gated territory anyway
(best-effort). Went looking for more places where `doldecomp-brawl` has
*actual* decompiled source (not just a name in `symbols.txt`) for code that
Brawlback's hooks touch, the same way `gf_pad_queue.cpp` paid off earlier.

**Found and cross-checked `src/sora/gf/gf_task.cpp`'s real `gfTask::process(ProcessType)`**
against `gfTaskProcessHook`/`gfTaskProcessHook2` (installed at
`0x8002dc74`/`0x8002dc78`, right at the top of this real function). The
real code is `if (taskType < 8) { switch(...) 8 cases } else { switch(...)
more cases }`. The hook's hand-written branch (`cmpwi task_type, 8` /
`bge END_OF_LOOP`, jumping to `0x8002dc7c` when `<8` and `0x8002dd1c`
when `>=8`) matches this exactly, and register assignment (`r3`→`task`
object pointer, `r4`→`task_type`) matches the real member-function calling
convention (`r3`=`this` for a member fn, `r4`=first real argument). **This
is a genuine positive verification, not just "looks plausible" - confirmed
correct against real decompiled logic**, unlike everything else in this
file that's still assumption-based.

**Also confirms `gf_pad_thread.cpp`'s real `gfPadReadThread::run()`** calls
`g_gfPadSystem->updateLow()` from a dedicated periodic-alarm thread during
normal (non-netplay) play - consistent with `fixPadInconsistency()`'s
`if (!Netplay::IsInMatch()) g_gfPadSystem->updateLow();` gating: the real
thread's automatic polling is deliberately left alone outside matches, and
Brawlback's own EXI-driven injection path takes over during them. Makes
architectural sense, no discrepancy found.

**Hit the real limit of this technique.** Checked for decompiled source
for `gfSceneManager` (touched by nearly every `NetMenu` hook -
`ChangeGfSceneField`, `ChangeStruct3Scenes`, `BootToScMelee`, etc.),
`ipSwitch`, `gfPadSystem` itself (not just its read-thread), and
`muWifiInterfaceTask` - none exist as real source anywhere in
`doldecomp-brawl`, only as names in the symbol table at best. The actual
decompiled (non-stub) coverage in this decomp project is concentrated in
stages (`mo_stage/st_*`) and a handful of low-level `gf` engine utility
classes (`gfTask`, `gfPadStatusQueue`, `gfPadReadThread`,
`gfSlowManager` - the last of these isn't even used by live Brawlback code)
- it does not reach the menu/scene/CSS/matchmaking layer that essentially
all of the remaining `NetMenu` namespace hooks live in. That's the
overwhelming majority of what's left unverified in this file.

**Where this leaves things:** the address-sanity, duplicate-address,
stack-frame, and hook-type-consistency checks (all documented above) cover
everything checkable without retail-code knowledge, and found 2 real bugs.
The `gfTask`/`gfPadReadThread` cross-checks are icing - genuine positive
verification where it was possible. Everything else in the raw-asm
`NetMenu` hooks (the CSS/quickplay/matchmaking-menu bypass logic) is
resting on the original author's own testing, not on anything checkable
from this environment - there's no decompiled source, stub or otherwise
meaningful, and no retail disassembly, for any of the scene-manager/CSS
code those hooks touch. Further progress here genuinely needs either
Ghidra access to the retail binary or a live test rig, not more reading.

## 2026-07-31 session (continued): a second real bug — wrong hook type for SkipDirectlyToCSS

Continued the "slow and boring work" with a different, complementary
technique: instead of verifying individual hook functions' internals,
cross-referenced `InstallHooks()`'s ~70 hook-registration calls against
`sy_core.h`'s own documented semantics for each hook type:

- `syInlineHook`/`syInlineHookRel`: **auto-returns** to the original code
  after the hook function returns normally.
- `sySimpleHook`/`sySimpleHookRel`: explicitly **does NOT** auto-return -
  the hooked function itself must manually resume execution (exactly why
  `setFrameAdvanceCounter`/`beginningOfFrameLoop`/etc. hand-tear-down their
  own stack frame and `bctr` into retail code, as covered above).
- `syReplaceFunc`: function-level replacement with real call/return
  semantics preserved (different mechanism entirely - confirmed this is
  why `Utils::ReturnImmediately`, a bare `blr`, is correctly used with it
  in three places to no-op some retail functions entirely).

Wrote a script that, for every `InstallHooks()` entry, checked whether the
target function is `naked`/contains a `bctr`-based manual resume, and
flagged any hook-type/function-shape mismatch. Out of ~70 hooks, found
exactly one: **`NetMenu::SkipDirectlyToCSS`** (commit `a581639`) was
installed via `sySimpleHookRel` but is a completely plain function - no
`naked`, no manual register save/restore, no `bctr` - just three field
writes and a `render()` call before falling off the end into a normal
compiler-generated `blr`. Per `sySimpleHook`'s own documented contract,
that `blr` would return to whatever stale address happened to be in the
link register rather than resuming the game's original code at the hook
point - a real control-flow corruption bug, plausibly a hang or crash
whenever this specific hook fires (the quickplay-CSS-skip path, given
`onQuickplayMenus`/`Modules::SORA_MENU_MAIN`).

The evidence for this being a copy-paste mistake rather than intentional:
its immediate neighbor in both the function definitions *and* the
`InstallHooks` call list, `SkipDirectlyToTrainingRoom`, is structurally
identical (same shape - no naked, no manual resume) and correctly uses
`syInlineHookRel`. Fixed by changing `SkipDirectlyToCSS`'s registration to
`syInlineHookRel` to match. Build-verified. Not live-tested.

Also worth noting for a future pass: `NetReport::netReportHook` through
`netMinReportHook` (5 functions) are referenced in `InstallHooks` but every
single one is commented out - so that whole namespace is entirely disabled/
unused right now, consistent with the already-documented pattern of
half-finished features in this codebase (like the #76 report code before
this session's fix). Didn't investigate further since nothing calls into
it either way.

This closes out the systematic cross-referencing pass on `InstallHooks()`
and the raw-asm hook functions - between the symbol-lookup address
verification, the duplicate-address structural scan, the disassembly-
verified stack frame fix, and this hook-type mismatch scan, this is about
as thorough an audit as is possible without actual Ghidra access to the
retail binary's logic itself (as opposed to just its address/symbol
layout, which is what all of today's checks worked from).

## 2026-07-31 session (continued): the "slow and boring work" pass — a real stack-corruption bug, found via disassembly not guessing

User explicitly asked to do the slow work on the raw-PowerPC-asm hooks I'd
been skipping as "needs Ghidra." Found a way to make a meaningful chunk of
that tractable without actual Ghidra:

**New tool discovered: real symbol databases already in this environment.**
- `lib/BrawlHeaders/RSBE01.lst` (in brawlback-asm itself) - ~1900 curated
  global-variable RAM addresses (e.g. `901812A0: g_GameFrame`,
  `805A0040: g_gfPadSystem`).
- `doldecomp-brawl/config/RSBE01_02/symbols.txt` - ~34,800 entries covering
  the *entire* static RSBE01 Rev 2 executable (exactly the revision this
  project targets), including demangled C++ names for known functions
  (`process__6gfTaskFQ26gfTask11ProcessType` = `gfTask::process(...)`,
  `update__13gfSlowManagerFv` = `gfSlowManager::update()`) and, even for
  unnamed functions, real addresses + sizes (`fn_XXXXXXXX`), which is
  enough to confirm a hardcoded hex address actually lands inside a real
  function boundary rather than nowhere.

Wrote a small lookup script (`/tmp/.../scratchpad/symlookup.py` -
session-local, not committed) and extracted every `lis`/`ori` address pair
used in `Rollback_Hooks.cpp`'s raw asm (62 unique addresses). Cross-checked
all of them: all land inside real function/object boundaries, nothing
pointing at garbage. Also ran a structural check across the whole file for
addresses reused across *different* hook functions (a classic copy-paste-
bug signature) - found 3 cases, traced each one, and all 3 turned out to be
legitimate shared retail re-entry points (multiple hooks converging back
into the same original code path), not bugs - e.g. `beginningOfFrameLoop`'s
natural continuation and `beginningOfFrameLoop2`'s one branch both correctly
resume at the same address inside the same large ~1400-byte retail function
(`fn_80016F8C`), which turns out to be heavily instrumented with many
separate injection points threaded through it.

**The actual find (commit `484c0a0`, high-confidence, disassembly-verified,
not just read-and-guessed):** Several hook functions manually tear down
their own compiler-generated stack frame before `bctr`-jumping into retail
code (since `bctr` skips the compiler's normal `blr` epilogue at the end of
the function, this has to be replicated by hand). Realized these hardcoded
teardown offsets are only correct if they match what the compiler *actually*
generated for that specific function's prologue - and that's independently
verifiable, since I have the real build output. Disassembled the built
`.elf` with `llvm-objdump -dr --no-show-raw-insn` (the `.o` files are LLVM
bitcode, not usable directly; the linked `.elf` works) and compared every
such block against its own function's real prologue.

Found one mismatch: `setFrameAdvanceCounter`'s teardown used offsets
`0x14`/`0xC`/pop-16 - but its real prologue (`stw 0,4(1)` /
`stwu 1,-32(1)` / `stw 31,28(1)` / `stw 26,8(31)`) is a 32-byte frame,
because this function's inline asm clobbers r26 (`:"3","26"`), forcing the
compiler to treat r26 as live across the whole function and give it its
own save slot - unlike every other function using this same teardown
pattern, which only save LR+r31 in a 16-byte frame. The offsets used
(`0x14`/`0xC`/16) are *exactly* what `beginningOfFrameLoop`'s real 16-byte
frame needs (verified that one separately - it's correct) - strong
evidence this was copied from there without adjusting for the extra saved
register. Confirmed via grep that `setFrameAdvanceCounter` is the *only*
function in the file whose inline asm clobbers a register in the r24-r31
range this way, so this is very likely the only instance of this specific
bug class here.

Impact, concretely: every time this function ran while `Netplay::IsInMatch()`
is true - i.e. during every active match, not a rare path - it restored LR
from the wrong stack slot (loading garbage into the link register via
`mtlr`), restored r31 from the wrong slot, never restored r26 at all
(harmless only because r26 happens to never actually be written to
anywhere in this function's body), and popped only half the real frame
size, permanently leaking 16 bytes of stack on every call along this path.

Fixed the offsets to the function's real frame layout and added the r26
restore for consistency with the same function's other exit path. Then
rebuilt and **re-disassembled the fix** to confirm it's now internally
consistent with the real prologue (LR from `36(1)`, r31 from `28(1)`, r26
from `8(31)`, frame popped by `32` - all exactly matching the real `stw`
offsets) - not just "compiles," actually verified against the compiled
frame layout. Documented the whole reasoning in a comment at the fix site
so a future session doesn't need to re-derive the disassembly technique.

**Still not live-tested** - this is arguably the single highest-priority
item in this whole session to verify once real testing is possible, since
it touches the link register on every in-match call. But unlike the #73/#75
class of "needs Ghidra, can't safely guess" problems, this one was fixable
with high confidence because the uncertainty was entirely about *this
project's own compiled output*, which is directly inspectable with the
build tools already in this environment - not about undocumented retail
game internals.

Checked the two other similar-looking manual-teardown blocks in the file
(`BBBootTosqNetAnyOkiraku`, and the two blocks inside `beginningOfFrameLoop`
itself) the same way - both confirmed correct, no further instances of this
bug found. Left `forceFriendCode`'s `lwz 0, 0x00F8(29)` alone - that one's
inside a genuinely `naked` function manipulating a caller-inherited frame
(register 29-based, not this function's own compiler-generated one), which
is a fundamentally different, real-retail-code question this technique
can't answer without actual retail disassembly - out of scope, same as the
rest of the raw-asm hooks.

## 2026-07-31 session (continued): swept the rest of `Brawlback-Online/source/`

Read every remaining source file in `Brawlback-Online/source/` not yet
covered this session: `EXI_hooks.cpp`, `exi_packet.cpp`, `utils.cpp`,
`BrawlbackHeadersImpl.cpp`, `StageFixes.cpp`, `rel.cpp`, `mem_exp_hooks.cpp`
(all small, 16-333 lines each). Didn't go further into the rest of
`Rollback_Hooks.cpp` (~1100 lines still unread) because most of what's left
there is raw inline PowerPC assembly hooking hardcoded retail addresses -
not safely reviewable via static reading without real disassembly/decomp
cross-reference for each address, same blocker as everything else that
needs Ghidra. Stuck to C++ logic, where static analysis actually works.

- **Fixed (commit `b5ed0bc`, minor/diagnostic-only):** `EXIPacket`'s no-arg
  and 1-arg constructors logged `size` (the class member - not yet
  initialized at that point in construction, since the failure path returns
  before `this->size = new_size;` runs) instead of the locally-computed
  `new_size`, in their allocation-failure `OSReport`. Only matters if
  `MEMAllocFromExpHeapEx` actually fails (rare, already-exceptional path),
  and only affects the accuracy of a log message, not program behavior -
  but a genuine copy-paste-style bug, safe one-line-per-constructor fix,
  build-verified.
- **Confirmed inert, not touched:** `BrawlbackHeadersImpl.cpp`'s
  `#if __cplusplus == 199711L` branch (an old-Metrowerks-compiler-compatible
  set of constructors/assignment-operators, presumably kept for a toolchain
  this project no longer actually uses) has an empty
  `PlayerSettings::PlayerSettings() {}` - in that branch specifically,
  `PlayerSettings`'s fields have no in-class default initializers (those
  only exist in the `#else`/modern-C++ branch this project's actual clang
  toolchain takes), so every field would be genuinely uninitialized garbage
  if this path ever actually compiled. Confirmed via the real build
  (`python3 ./bbk.py setup && make`, this whole session) that the active
  toolchain is clang with a modern C++ standard, so `__cplusplus` is never
  literally `199711L` and this branch is dead for the real build - same
  category as the `containers/Settings` dead files found in the launcher
  earlier this session. Left alone, but worth knowing if this project ever
  gets built with an actual Metrowerks/CodeWarrior toolchain again.
- `EXI_hooks.cpp`, `utils.cpp`, `StageFixes.cpp`, `rel.cpp`,
  `mem_exp_hooks.cpp`: read in full, all clean. `utils.cpp`'s `myMemmove`
  is dead code (defined, never called anywhere - confirmed via grep and the
  build map) but not itself buggy, so nothing to do there either way.
  Double-checked `MemExpHooks::getFreeSize()`'s zero-arg call site in
  `myMemmove` before flagging it as a possible bug - it has default
  arguments (`heap = mainHeap, alignment = 4`) declared in
  `mem_exp_hooks.h`, so that one's a non-issue, false alarm caught before
  reporting it.

`Brawlback-Online/source/`'s C++ logic is now essentially fully read this
session (everything except the untouched, decomp-gated raw-ASM tail of
`Rollback_Hooks.cpp`). Total fixes across this whole "more ASM work"
stretch: the game-report enable (`975ecd5`), the pad-queue precedence bug
(`374cf3f`), and this logging fix (`b5ed0bc`) - all build-verified, none
live-tested, consistent with what's achievable without the PC the user is
away from for ~6 days.

## 2026-07-31 session (continued): operator-precedence bug in the pad-queue wraparound check

Continued the ASM audit into other parts of `Rollback_Hooks.cpp` not yet
carefully traced this session. Found two compiler-flagged
`-Wparentheses` warnings in `GameLoop`/`ProcessGameSimulationFrame`
(`'&' has lower precedence than '!=' / '!='  will be evaluated first`) and
worked out both by hand rather than assuming they're equally bug-worthy:

- **Line ~607 (real bug, fixed, commit `374cf3f`):**
  `if(queue_param1 != queue_param2 + 1 & 3)` parsed as
  `(queue_param1 != queue_param2 + 1) & 3` - a 0/1 boolean ANDed with 3,
  which is always just the boolean unchanged, so the `& 3` never actually
  masked `queue_param2 + 1` as presumably intended. This code manually
  pre-processes Brawl's own `gfPadStatusQueue` (raw pointer arithmetic on
  a hardcoded live-instance address, `0x805ba480`) before calling
  `push_gfPadStatusQueue` - reads `m_front`/`m_back` by raw offset,
  decrements/wraps `m_back` in a local var, and conditionally commits that
  decrement back to the real struct. Before touching this, pulled up
  `doldecomp-brawl`'s real decompiled `gfPadStatusQueue::push()`/`pop()`
  (`src/sora/gf/gf_pad_queue.cpp` - concrete named-field source, not a
  stub) to confirm this subsystem genuinely uses mod-N ring-buffer
  wraparound (`(m_back + 1) % NRows`, matching the local code's own
  explicit `queue_param2 = 3` wrap value), rather than guessing blind.
  With the bug, the only case that actually changes is when
  `queue_param2 == 3` (wraps to compare against `4`, which the front index
  - always 0-3 - can never equal, so the commit always fired there instead
  of correctly comparing against the wrapped value `0`) - a narrow-window
  queue-consistency bug, exactly the flavor of "occasional instability in
  long rollback-heavy matches" this project keeps chasing. Fixed by adding
  the parens the warning suggests. Build-verified (warning count dropped
  by exactly one, as expected); **not live-tested** - this runs every
  single simulation frame, so this is a good candidate to watch closely
  once real testing is possible.
- **Line ~966/979 (false alarm, left alone):** `(...) >> 2 & 1 != 0`
  parses as `(...) >> 2 & (1 != 0)`. Since `1 != 0` is the compile-time
  constant `true`/`1`, this reduces to `(shifted value) & 1` - which, used
  directly in a boolean `if` condition, is behaviorally identical to the
  presumably-intended `((shifted value) & 1) != 0` (both are truthy iff
  that one bit is set). No actual behavior difference between the two
  readings here, so "fixing" it would be pure churn with zero functional
  change - left as-is rather than touching working code for cosmetics.
  Worth remembering this distinction for any future `-Wparentheses`
  warning in this codebase: work out both parsings by hand before
  assuming a fix is needed, since not all of them are.

## 2026-07-31 session (continued): issue #76 — fixed and enabled the end-of-match report to Lylat

Following the #73 negative-result work below (which didn't produce a code
fix), looked for a genuine fixable bug elsewhere and found one: issue #76's
"report winner to Lylat" piece. `Match::PopulateGameReport`'s real
stock/damage-reading logic was entirely `/* ... */`-commented out (leaving
`report.stocks[]`/`report.damage[]` as uninitialized stack garbage), and its
only call site in `StopGameScMeleeHook` was additionally wrapped in
`#if 0` - so the game has never once sent `CMD_MATCH_END` to Dolphin.

Root cause of why it was disabled, not just "unclear why" anymore: the
commented-out draft called `fighterManager->getFighter(entryId)` with one
argument, but `ft_manager.h` declares `getFighter(int entryId, int
instanceIndex)` - two required arguments. That's a real compile error if
naively uncommented, which is almost certainly why someone commented it out
and never finished it, rather than a deliberate feature flag. Fixed
(commit `975ecd5`) by using the exact pattern already proven live and
working two functions above it in the same file
(`Util::PopulatePlayerFrameData`): `fighterManager->getOwner(entryId)`
directly gives an `ftOwner*` with `getDamage()`/`getStockCount()` - no need
to fetch a `Fighter*` via the broken call at all. Removed the `#if 0`
wrapper so it actually runs.

Checked the Dolphin-side receiver before enabling this, since it's never
been exercised with real data before: `EXIBrawlback.cpp`'s `handleEndMatch`
is fully implemented and POSTs a real JSON report (uid, playKey, per-player
damage/stocks) to `https://lylat.gg/reports` - a real, live endpoint, not a
stub. It was just unreachable because the ASM side never sent anything.
This significantly de-risked the fix - the receiving end was already done
and presumably tested against something at some point, just never actually
fed real data.

Build-verified: full `python3 ./bbk.py setup && make` from the repo root
links cleanly (`Brawlback-Online.rel` output), only the same pre-existing
unrelated warnings as before this change. **Not live-tested** - confirming
the POST actually lands correctly on lylat.gg's side, and that
`this->matchmaking->GetPlayerInfo()[i]` on the Dolphin side has real uids
to pair against by the time this fires, both need an actual match to
verify. This addresses one of #76's three bundled asks (report to Lylat);
the other two (return to CSS with player info, live-update opponent's
screen on character change) are still open, same as before - those are
new subsystems, not a comment-and-flag fix like this one was.

## 2026-07-31 session (continued): issue #73 deep-dive — thorough negative result, ruled out 4 hypotheses

User is stepping away for ~6 days and asked to continue with "more ASM work"
in the meantime. Re-added `skanderbm123/ssbb-decomp` to this session (it
wasn't in scope by default) to check whether it could unblock any of the
Ghidra/decomp-gated issues.

**`ssbb-decomp` reality check: it does NOT help with #72/#73/#75/#76.**
It's the real `doldecomp/brawl` project, forked and genuinely useful for the
Stadium fix (confirmed - `mo_stage/st_stadium` and its `include/st_stadium`
headers are real, named-field decompiled code). But menu/CSS/character-select
code (`sora_menu_sel_char`, `sora_adv_menu_name`, etc.) all live under
`src/mo_stub/` - auto-generated placeholder stubs like
`int fn_10_1C3E0(void* p) { return *(int*)((char*)p + 12); }`, not real
decompiled/named code, and there's no `mu_select_character_name_entry`
anywhere in the tree. `include/lib/BrawlHeaders` in this repo is the exact
same opaque-blob header set already checked in `brawlback-asm` (`char
_0[0x94]` for the name-entry widget, no named fields). So decomp coverage is
real but uneven: stages got real work, menus/CSS didn't. This doesn't move
#72's remaining piece (CSS text-entry offsets) forward.

**Given that, spent the "more ASM work" time on the most promising still-open
lead: issue #73 (costume desync, "P2 loading in as the first secret
costume"), following up on the explicit next-step the 2026-07-29 correction
left behind** ("worth checking next whether `this->gameSettings` on the host
stays intact between its own broadcast and the client's reply, and whether
the ASM-side P1/P2 assumptions line up with this struct's layout"). Traced
the *entire* pipeline end to end, across both repos, via pure static
analysis (no live test, no decomp needed for any of this - all of it is
named, readable code):

1. **ASM capture** (`Rollback_Hooks.cpp::fillOutGameSettings`, called from
   `setNextAnyOkirakuCaseFive` on "Loaded into online training room", i.e.
   well after CSS closes): reads `g_GameGlobal->m_selCharData->m_playersInitData[0].m_colorNo`/`.m_colorFileNo`
   - always index `[0]`, always writes to `settings.playerSettings[0]`.
   Confirmed via `gm_sel_char_data.h` that `m_playersInitData` here is typed
   `gmPlayerInitData[7]` - **the exact same struct type**
   (`gm_global_mode_melee.h`) later written to on the apply side, not a
   different same-shaped-but-differently-ordered struct. No mismatch risk
   here.
2. **Ordering hypothesis, tested and disproven**: `setNextAnyOkirakuCaseFive`
   sends `CMD_START_MATCH` (own settings) **before** `CMD_FIND_OPPONENT`
   (which is what triggers matchmaking, connection, and eventually the
   host's first broadcast) - confirmed by reading the function directly,
   they're two sequential `EXIPacket::CreateAndSend` calls in program order
   on the same EXI channel, same CPU thread. So there's no race where a
   client's own local `gameSettings.playerSettings[0]` could still be
   default/unpopulated when the host's broadcast arrives - I initially
   suspected exactly this (would explain "first secret costume" as a
   zero-initialized `charColor`/`colorFileIndex` default) but the code
   rules it out.
3. **Dolphin-side merge** (`EXIBrawlback.cpp::ProcessGameSettings`, both
   branches read in full): traced host and non-host paths separately.
   Non-host: copies its own local data (currently in slot 0) into slot 1,
   then overwrites slot 0 with the opponent's (host's) data, then
   broadcasts this *fully-merged* 2-player struct back - correct. Host:
   receives that merged reply, pulls the client's real data from slot 1 of
   it (which the non-host path guarantees is correct), leaves its own slot
   0 untouched. Also confirmed `this->gameSettings` (the persistent member
   both `handleStartMatch`'s memcpy and `ProcessGameSettings` operate on)
   is only ever touched from two call sites that run on the same thread in
   guaranteed sequence (`NetplayThreadFunc`'s initial broadcast, then later
   its own main loop's `ProcessNetReceive` → `ProcessGameSettings`) - no
   cross-thread race on it either.
4. **Shared-header version-skew hypothesis, tested and disproven**: the
   `GameSettings`/`PlayerSettings` wire structs come from the
   `Brawlback-Team/brawlback-common` submodule, included independently by
   both repos. Checked both pins directly (`git ls-tree` on the Dolphin
   repo's submodule entry vs. `git rev-parse HEAD` inside brawlback-asm's
   checkout of the same submodule) - **identical commit,
   `33d728c8d047374e63ec94bfd9eb11061c1f7953`**, on both sides. No drift.
   (Side note while checking this: the Dolphin clone's submodule directory
   was sitting completely uninitialized/empty this whole session until now
   - a plain `git clone` doesn't pull submodules. Worth remembering for any
   future session working in `/workspace/brawlback-team-dolphin`:
   `git submodule update --init --recursive` if something under
   `include/brawlback-common` or similar looks missing.)
5. **ASM re-application** (`CheckIsMatched` → `MergeGameSettingsIntoGame` →
   `GMMelee::PopulateMatchSettings` → `FillInMeleeObj`): re-read all four in
   sequence, confirmed straightforward `[0]`/`[1]`-indexed copies at every
   step with no swap, no off-by-one, and (per the 2026-07-29 fix)
   endianness-corrected before any of this runs. `FillInMeleeObj` writes
   `g_globalMelee.m_playersInitData[i].m_colorNo = costumeChoices[i]` /
   `.m_colorFileNo = fileIndexChoices[i]` directly, same struct type as
   point 1, no aliasing.

**Conclusion: the entire netcode/merge pipeline, on both the ASM and
Dolphin sides, is internally consistent and correctly indexed for
`charColor`/`colorFileIndex`, as far as this session can inspect it.**
This isn't a fix, but it's a real result: it rules out four concrete,
plausible hypotheses (index inversion - already known from the earlier
revert, start-order race, struct-type mismatch, submodule version skew)
with actual evidence rather than leaving them as open guesses for the next
session to re-derive. What's left, genuinely needing either a live two-client
repro or Ghidra-level tracing (same blocker as before, now narrowed): **how
Brawl's own internal code actually consumes `m_colorNo`/`m_colorFileNo` to
pick a costume file** - that logic is not decompiled anywhere accessible to
this session (confirmed above, `ssbb-decomp` doesn't cover it), so a bug
there is invisible to static analysis of this repo alone. If a future
session gets Ghidra or better decomp coverage of Brawl's character/costume
loading code, that's exactly where to look next - not the netcode merge
logic, which this session now has high confidence is not the culprit.

## 2026-07-31 session: launcher `main.ts` audit — broken `brawlback://` deep link

Continuing the code-level audit of `brawlback-launcher` (local clone at
`/workspace/brawlback-launcher`, no fork exists yet, see below for why). Per
the user's explicit instruction this session ("skip what needa a real pc for
now and contonue working on it, ill do an intensive test when i can"),
continuing to work through files that can be verified without a live PC test.

**Bug found and fixed in `src/main/main.ts` (commit `50bdba7` in the local
launcher clone):** `electron-builder.json`'s `"protocols"` config registers
the OS-level custom URL scheme as `"brawlback"` — that's what Windows/macOS/
Linux actually hand the app when a `brawlback://...` link is opened. But
`main.ts` had a leftover Slippi constant `slippiProtocol = "slippi"` and
checked incoming URIs against `slippi:`. Since the OS never sends a
`slippi://` URI (it sends `brawlback://`), the check always failed, the
handler fell through to treating the whole URI string as a local file path
(`fileExists(aUrl)`), that always failed too, and the function returned
having done nothing. Net effect: the entire deep-link / "download and open
replay from a URL" feature was silently dead — clicking a `brawlback://`
link anywhere (e.g. a future lylat.gg "open in launcher" button) would do
absolutely nothing, no error, nothing in the UI. Fixed by renaming the
constant to `brawlbackProtocol = "brawlback"` and fixing the comparison/
switch-case to match. Also renamed `handleSlippiURI(Async)` →
`handleBrawlbackURI(Async)` and fixed a "Only allow a single Slippi App
instance" leftover comment, consistent with the branding sweep pattern
established earlier.

Left alone (flagged with a comment, not guessed): inside that same handler,
the actual replay download URL is hardcoded to
`https://storage.googleapis.com/slippi.appspot.com/${replayPath}` — this is
Slippi's own real GCS bucket, serving Melee replays only. No Brawlback/Lylat
equivalent bucket is known to exist (matches the earlier-established finding
that Brawlback's replay format/hosting isn't defined yet). So even with the
protocol fix, a real `brawlback://download?path=...` link would still fail
at the download step — that's expected/correct until Brawlback or Lylat
stands up its own replay storage. Did not invent a URL for this.

Verified via `npx tsc --noEmit -p tsconfig.json` (clean, exit 0) — no live
PC test possible for the deep-link OS integration itself (needs an actual
OS-level protocol registration + click), consistent with what's deferred
this session per the user's instruction.

Continued the same session: read the rest of `src/main/` —
`preload.ts`, `api.ts`, `ipc.ts`, `setup.ts`, `github.ts`, `verifyIso.ts`,
`newsFeed.ts` (already fixed earlier), `util.ts`, `installModules.ts`,
`types.ts` (empty file, unused). All clean / already-correct except one
minor pre-existing data issue, not code-fixed:

- `src/main/verifyIso.ts`: `isoHashes.set("0e95949ac585f357e79fcd34b20670b5dca97ac2", { valid: VALID, name: "unknown Brawl copy sha1 hash" })`
  is a 40-hex-char SHA1-length key, but the only function actually wired up
  (`verifyIsoMD5`, called from `setup.ts`'s `ipc_checkValidIso` handler)
  looks up 32-hex-char MD5 digests. This entry can never match, so whatever
  Brawl copy that SHA1 was recorded for will show as "unknown" instead of
  "valid" in the ISO validator. Harmless (falls back to UNKNOWN, not
  INVALID), and there's no way to recover the correct MD5 for that copy
  without the actual ISO file, so left as-is rather than guessing a hash.
  The dead `_verifyIsoSHA1` function in the same file (imported in
  `setup.ts` but never called) is the leftover legacy SHA1-based verifier
  this key used to belong to, per its own "legacy" comment.
- `SavedConnectionsList.tsx`'s `getLatestGithubReleaseVersion("project-slippi", "Nintendont")`
  call was re-confirmed still dead/unreachable (same `MainView`-tree
  console-mirroring code already established as unreachable earlier this
  session) — not re-fixed, consistent with that earlier finding.

`src/main/` is now fully read and audited.

**Continued the same session, `@dolphin` module (17 files, all read):**

- **Fixed, live bug (commit `3203934`):** `dolphin/setup.ts`'s
  `checkDesktopAppDolphin` handler — reachable on *every app launch* via
  `useApp.ts`'s init hook — still checked
  `app.getPath("appData")/"Slippi Desktop App"`, the real Slippi folder,
  while the sibling `deleteDesktopAppPath` in `main/setup.ts` had already
  been fixed to `"Brawlback Desktop App"` in an earlier commit. On any
  machine where the user has genuinely used real Slippi before (plausible —
  Melee and Brawl modding communities overlap a lot), this falsely
  triggered Brawlback's QuickStart "Import old Dolphin settings" step
  (`ImportDolphinSettingsStep.tsx`), which is a real, live-reachable
  onboarding screen offering to import the *Melee* Slippi Dolphin's
  netplay/playback settings into the Brawl-based Dolphin instance. Fixed to
  match the already-corrected path. Also fixed a stale `"DolphinQt.app"`
  comment in `dolphin/ipc.ts` left over from the corrected macOS bundle
  name (see `fb26d04` from earlier this session).
- **Confirmed root cause for a known gap, not fixed (needs a real feature
  build, not a bug fix):** `dolphin/instance.ts`'s `PlaybackDolphinInstance.play()`
  always launches Dolphin with `-i <commPath>` (a JSON comm-file handshake,
  Slippi's own custom CLI flag for replay-communication/playback control).
  Checked the actual Brawlback/Lylat Dolphin fork's CLI parser
  (`/workspace/brawlback-team-dolphin/Source/Core/UICommon/CommandLineParse.cpp`)
  — it has no `-i`/`--input` option at all, and a repo-wide search found no
  `ReplayComm`/`PlaybackStatus`-equivalent class anywhere in that Dolphin
  fork. So the launcher's entire "watch replay" pipeline
  (`playReplayAndShowStats` in `main.ts`, `PlaybackDolphinInstance`, this
  `-i` flag) points at a Dolphin-side feature that was never ported over
  from Slippi. This is concrete evidence for (and sharpens) the
  already-known "replay format/spectating needs Brawlback's own format
  decisions" punch-list item — it's not a small fix, it needs real design +
  C++ work on the Dolphin side. Not attempted here.
- Also confirmed at the code level (further hard evidence for the same
  known gap): `replays/loadFile.ts` imports and calls `SlippiGame` directly
  from `@slippi/slippi-js` to parse local replay files — i.e. the local
  Replay Browser page literally has zero Brawl-specific work done; it's
  Melee's own `.slp` binary parser, completely unported. Any real
  Brawlback replay file (whatever format eventually gets chosen) would fail
  to load here. Same underlying gap as the point above, just the "browse
  local files" side of it rather than the "watch via Dolphin" side.
- `config/geckoCode.ts`'s line-filter (`line.length !== 0 || line[0] !== "#"`)
  is backwards (should be `&&`, not `||` — as written it never filters
  anything, comments/blank lines pass straight into the Gecko-code line
  parser). Confirmed dead code though: the only caller chain
  (`config/config.ts` → `dolphin/util.ts`'s `updateBootToCssCode`) has zero
  callers anywhere in the codebase, same as the already-established
  "Boot to CSS" dead-code finding from earlier this session. Left unfixed,
  consistent with not touching confirmed-unreachable code.
- `dolphin/manager.ts`, `api.ts`, `ipc.ts`, `types.ts`, `config/config.ts`
  (the reachable parts), `install/download.ts`, `install/fetchLatestVersion.ts`
  (already fixed earlier this session, re-confirmed correct) all clean.

**`@settings` module (6 files, all read):** all clean — `settingsManager.ts`'s
earlier no-op-filter fix (`8270999`) confirmed still correct, `defaultSettings.ts`
already fully rebranded, `setup.ts`/`api.ts`/`ipc.ts`/`types.ts` are just
wiring/type declarations with nothing Slippi-specific left.

**`mod/` module (2 files, both read):** clean. `mod/util.ts`'s
`fetchModLatestVersion` stub for `DefaultMods.ProjectPlus` (no `downloadUrl`,
`//TODO add api calls`) is the same already-known "Project+ has no working
download/hosting" limitation, not something fixable from the launcher side.

**`@console`/`@broadcast` modules: deliberately not audited.** Verified
`renderer/views/MainView.tsx` (which is what routes to `Console` and
`SpectatePage`, the only UI entry points into these two main-process
modules) is never imported/rendered anywhere in the app — confirmed via a
repo-wide grep for `MainView`, only its own definition matches.
`renderer/App.tsx` mounts `HomePage`/`ReplayBrowserPage` directly instead.
This matches (and generalizes) the earlier-established "`SavedConnectionsList.tsx`'s
Nintendont reference is unreachable dead code" finding from before this
session — the entire console-mirroring and broadcast/spectate feature set
is unreachable from the live app, not just that one file. Not worth
auditing further until/unless that UI gets wired back up.

**`replays/` module (all 9 files, now fully read):** no new distinct bugs —
`loadFolder.ts`, `folderTreeService.ts`, `setup.ts`, `ipc.ts`, `types.ts`,
`replays.worker.ts`/`replays.worker.interface.ts`, `api.ts` are all
format-agnostic plumbing (fine) except `replays.worker.ts`'s
`calculateGameStats`, which — like `loadFile.ts` already documented above —
directly instantiates `@slippi/slippi-js`'s `SlippiGame` and calls
`game.getStats()`. Same already-recorded gap (zero Brawl-specific replay
format/stats work exists yet), not a separate new finding. Also worth
noting for whenever that format work happens: `calculateGameStats` hard-
requires exactly 2 players (`if (settings.players.length !== 2) throw`),
inherited from Slippi's 1v1-only stats engine — Brawl matches commonly have
more than 2 players, so this constraint will need revisiting as part of
that future design work, not fixable in isolation now.

`src/main/`, `@dolphin`, `@settings`, `mod/`, and `replays/` are now all
fully read and audited this session.

**IMPORTANT CORRECTION, same session — the renderer's real live app shell is
NOT what it looks like from `views/MainView.tsx`.** Read `renderer/App.tsx`
(the actual root) directly instead of assuming from `views/`, and found:

```
routesObj = [
  { path: "/", element: <AppBase />, children: [
      { index: true, element: <HomePage /> },
      { path: "replays/*", element: <Replay /> },   // <Replay/> = a literal
                                                      // `<h3>Replay</h3>` stub
                                                      // defined right in App.tsx
      { path: "settings/*", element: <SettingsPage /> },
  ]},
  { path: "/main/*", element: <Navigate replace to="/" /> },
]
```

So the *actual* live pages are `pages/base/AppBase.tsx` (→ `Menu.tsx` +
`UserHeader.tsx` + `PlayButton.tsx` + an `<Outlet/>`), `pages/home/HomePage.tsx`,
and `pages/settings/SettingsPage.tsx` — a genuinely Brawlback-specific shell
(`Menu.tsx` has a real Brawlback logo and mod-aware "Replays → vBrawl / P+"
nav entries, not inherited Slippi structure at all). The `/replays/*` route
is currently just the inline placeholder component, **not**
`@/containers/ReplayBrowser/ReplayBrowserPage` — confirmed via grep that
`ReplayBrowserPage` is only ever imported by `views/MainView.tsx`.

And `views/MainView.tsx` itself is dead (confirmed earlier this session via
grep — never imported anywhere outside its own file). Pulling on that
thread further: `views/LandingView.tsx` (which renders the `QuickStart`
flow, including `ImportDolphinSettingsStep.tsx`) is **also** dead by the
same test — grepped for `LandingView` repo-wide, only its own definition
matches, nothing imports it. Net effect: essentially all of `renderer/views/`
and everything solely reachable through it — `MainView`, `LandingView`,
`Console`, `SpectatePage`, `QuickStart` (including `ImportDolphinSettingsStep`),
`Header`, and the real `ReplayBrowser`/`ReplayBrowserPage` — is currently
orphaned from the live routing. This reads as a mid-refactor state: `pages/`
is the newer, actively-being-built Brawlback-specific shell; `views/` +
the `containers/` it pulled in is the older, more Slippi-shaped structure
being replaced, not yet deleted.

**This means the `checkDesktopAppDolphin`/"Slippi Desktop App" path fix
documented above (commit `3203934`) is NOT currently a live bug** — its
only consumer, `ImportDolphinSettingsStep.tsx`, is unreachable via the
same dead-`LandingView` chain. Correcting that characterization here rather
than editing the commit message: the fix itself is still correct and worth
keeping (harmless, consistent with the sibling `main/setup.ts` fix, and
will matter immediately if/when the QuickStart flow gets reconnected to
live routing), it just isn't fixing something a real user would hit today.
The `main.ts` `brawlback://` protocol fix (`50bdba7`) is unaffected by this
correction — OS-level deep-link handling doesn't go through React routing
at all, so that one remains a genuine live-reachable fix.

**Revised remaining scope, reflecting what's actually live:** the real
next audit targets are `pages/base/AppBase.tsx`, `Menu.tsx`, `PlayButton.tsx`,
`UserHeader.tsx`, `pages/home/HomePage.tsx`, `NewsFeed.tsx`, `TwitterFeed.tsx`,
and `pages/settings/SettingsPage.tsx` — not the `containers/`/`views/` tree,
which is confirmed unreachable except where something under `containers/`
is imported directly by one of the `pages/` files (worth checking file-by-
file rather than assuming by directory name).

**Continued the same session, all of the above now read.** All clean except:

- **Real fix (commit `dbe045b`):** `components/MultiPathInput.tsx` (used by
  `containers/Settings/ReplayOptions.tsx`, which IS live — confirmed via
  `SettingsPage.tsx`'s "Replays" tab, which is one of `pages/settings/`'s
  four real tabs). Its `assertValidPath` had broken logic for the case
  where a newly-added directory is a *parent* of an already-tracked one:
  it called `updatePaths(pathsToCheck.splice(i, 1))` — `Array.splice()`
  returns the *removed* elements, not the remaining array, so this passed
  a single-element array to `updatePaths` (which fully replaces the stored
  `extraSlpPaths` setting) as if it were the complete desired list. In
  practice this got immediately overwritten by `onAddClick`'s own
  `updatePaths([...paths, newPath])` call (using the stale, pre-removal
  `paths` closure), so the net effect was just that de-duplication silently
  never worked — but the two calls are two independent, unawaited IPC round
  trips with no ordering guarantee enforced in this code, so there was a
  real latent risk of the wrong one landing last and truncating a user's
  entire "Additional Replay Directories" list down to one entry. Rewrote
  `assertValidPath` to return the correctly-deduplicated list (or `null` to
  reject) instead of mutating/dispatching as a side effect, and centralized
  the single `updatePaths` call in `onAddClick`.
- **Cosmetic, same commit:** renamed user-facing "SLP" labels to "Replay" in
  `ReplayOptions.tsx` and `MultiPathInput.tsx` ("Root SLP Directory" →
  "Root Replay Directory", etc.) — Brawlback's replay format isn't `.slp`
  (that's Melee/Slippi's, and per the finding above it isn't decided yet
  for Brawlback), so the label was actively misleading even though the
  underlying directory-picker mechanics are format-agnostic and already
  correct. Did *not* touch the many non-live "SLP" references under the
  confirmed-dead `containers/ReplayBrowser/`, `containers/Console/` (e.g.
  `AddConnectionForm.tsx`'s "SLP files"), or internal-only comments/variable
  names (`rootSlpPath` etc.) — those are either unreachable or not user-
  facing, consistent with not touching what doesn't need touching.
- `pages/base/AppBase.tsx`, `Menu.tsx` (real Brawlback-branded nav, already
  correct), `pages/home/HomePage.tsx`, `NewsFeed.tsx` (already fixed
  earlier this session, re-confirmed live and correct), `news_article/NewsArticle.tsx`,
  `containers/Settings/MeleeOptions.tsx`, `DolphinSettings.tsx` all clean.
  `pages/home/TwitterFeed.tsx` confirmed dead (defined, never imported).
- Noted but not fixed, matching the already-established "accounts backend
  needs Lylat's real info" gap: `UserHeader.tsx`'s "Log in" button is a
  literal no-op (`onClick={() => console.log("login")}`), and
  `services/auth/auth.service.ts` is unmodified Slippi Firebase auth code
  reading `process.env.FIREBASE_*` — there's no known Brawlback/Lylat
  Firebase project to point this at, so this is expected-incomplete, not a
  fixable bug in isolation.
- Noted but not fixed (small, live, cosmetic-severity, same category as
  other stubs): `containers/Settings/ModsOptions.tsx`'s per-mod Edit icon
  (`onEdit={(id) => console.log(id)}`) is a no-op — clicking it does
  nothing. Not a data-loss risk like the MultiPathInput bug, just an
  unimplemented affordance.

**Renderer live-surface audit is now essentially complete**: everything
reachable from `App.tsx`'s actual routes (`AppBase` → `Menu`/`PlayButton`/
`UserHeader` chrome, `HomePage`/`NewsFeed`, `SettingsPage`'s 4 tabs and
their `containers/Settings/*` implementations, plus `useMods`/`useAccount`/
`useDolphinActions`/`auth.service.ts` in `lib/`/`services/`) has been read.

**Continued the same session, found the biggest branding bug yet (commit
`5f81251`).** Tracing `App.tsx`'s `!initialized` early-return path (shown
before the main UI mounts, on literally every single app launch) led to
`views/LoadingView.tsx`, which is genuinely live despite living under
`views/` (the dead-tree assumption doesn't hold file-by-file — checked and
confirmed this one specifically). It renders `LoadingScreen` from
`components/LoadingScreen.tsx`, which used `BouncingSlippiLogo` as its
spinner icon, and `LoadingView.tsx` itself applied `withSlippiBackground` as
a page watermark. Both loaded the real `slippi-logo.svg` asset — meaning
the actual Slippi logo, animated (bounce + barrel-roll on hover) plus a
faint full-page watermark copy of it, was the first thing every Brawlback
user saw on every single launch, before anything else in the app even
loads. This is a strictly worse instance of the same class of bug as the
Discord-link fix from earlier this session, just far more visible (100% of
launches vs. a menu item).

Fixed by renaming `BouncingSlippiLogo` → `BouncingBrawlbackLogo` and
`withSlippiBackground` → `withBrawlbackBackground` (files, exports, and all
call sites, including updating the two additionally-affected but currently-
dead `views/SettingsView.tsx` and `views/LandingView.tsx` for consistency),
swapping in the existing `brawlback_padding_dark.png` asset (already used
correctly elsewhere, e.g. `Menu.tsx`'s sidebar logo) in place of
`slippi-logo.svg`. `npx tsc --noEmit` clean throughout.

Also confirmed and left alone (dead code, only reachable via the
already-established-dead `containers/Header/index.tsx`): one more
`slippi-logo.svg` reference there, plus a `services/slippi/slippi.service.ts`
GraphQL client — that one's actually fine as-is, its backend URL is read
from `process.env.SLIPPI_GRAPHQL_ENDPOINT` (parameterized, not hardcoded),
so it's just inert without real Brawlback/Lylat backend env vars configured
— same already-known "accounts backend needs Lylat's real info" gap, no new
finding.

**Lesson reinforced this pass: "lives under `views/`" is not sufficient
grounds to assume dead code** — `LoadingView.tsx` is the counterexample.
Each file's actual import chain back to `App.tsx` needs checking
individually; the earlier `views/`-is-dead generalization was directionally
right (most of it is) but not a safe blanket rule.

Remaining truly-unexplored surface is limited to: `containers/Settings/AdvancedAppSettings.tsx`,
`BuildInfo.tsx` (now confirmed live via `LoadingView.tsx`, read and clean —
see the `BuildInfo` note above), `HelpPage.tsx`, `SupportBox.tsx`,
`SettingItem.tsx` (confirmed live, imported by all 4 live Settings tabs),
`Settings/index.tsx`, `Settings/types.ts` (reachability of the remaining
unread ones not yet individually confirmed — check each against `App.tsx`'s
real import graph rather than assuming by directory), plus a handful of
shared `components/` (`PathInput`, `Checkbox`, `ConfirmationModal`,
`IconMenu`, `ExternalLink`, `MarkdownContent`, `DevGuard`, `Message`) that
appeared as dependencies above but weren't opened themselves.

**Continued the same session: read all of the remaining shared
`components/`** (`SettingItem.tsx`, `PathInput.tsx`, `ExternalLink.tsx`,
`ConfirmationModal.tsx`, `Message.tsx`/`IconMessage`, `FormInputs/Checkbox.tsx`,
`IconMenu.tsx`, `MarkdownContent.tsx`, `DevGuard.tsx`) — all clean, generic,
no Slippi leftovers or bugs found. `DevGuard`'s advanced-user Easter egg
(`BuildInfo`'s 7-click toggle, gates one Linux-conditional Dolphin-path
setting in `DolphinSettings.tsx`) is currently only reachable via the
dead `containers/Settings/index.tsx`/`SettingsView` chain — `LoadingView.tsx`'s
`<BuildInfo />` call doesn't pass `enableAdvancedUserClick`, so on the live
app this toggle can't currently be triggered by a user at all. Not a bug,
just a natural consequence of the already-documented dead-code split; no
action taken.

**This closes out the live-renderer audit for this session.** Every file
reachable from `App.tsx`'s real routes has now been read: the `pages/`
shell, all 4 `SettingsPage` tabs and their `containers/Settings/*`
implementations, and every shared `components/` dependency they pull in.
Took the `LoadingView.tsx` lesson seriously and individually grep-verified
the last 5 unconfirmed files rather than assuming by directory:
`Settings/index.tsx` is imported only by the confirmed-dead `SettingsView.tsx`;
`AdvancedAppSettings.tsx` and `HelpPage.tsx` are imported only by that same
`Settings/index.tsx`; `SupportBox.tsx` isn't imported by anything at all
outside its own file (fully orphaned, not even wired into the dead
`Settings/index.tsx`). All confirmed genuinely dead, not just assumed.
`Settings/types.ts` is just type declarations, not independently a
reachability question. The live-renderer audit is complete.

## 2026-07-28 session: THE REAL DOLPHIN FORK, and a real root cause for #1

**Read this section before trusting anything below dated 2026-07-27 or
earlier about "Project-Plus-Dolphin" being the emulator fork that pairs with
this ASM code. It's wrong, or at least more complicated than it looked. This
section corrects and complicates it, in that order — read both parts.**

### An important wrinkle: which line is actually "current" for the team?

Read `Brawlback-Team/brawlback-wiki`'s `FAQ.md` (cloned at
`/workspace/brawlback-wiki`) before assuming everything below is a clean,
settled correction. It's dated "Current state of affairs 2026" and says,
verbatim:

> The Alpha release will be a PR into P+ Dolphin with an additional syringe
> plugin... It will be like normal netplay where you'll open Netplay, host a
> lobby, and send a connect code to your opponent... **Essentially the Alpha
> will just be regular netplay but using Rollback instead of Delay Based, no
> matchmaking, no custom menu's, etc.** This is being done to get the netcode
> out faster...
>
> The Full release will be a much more Slippi-eqsue experience. A launcher
> for the application, in-menu matchmaking, fixed replays for rollback...

Read plainly, this says: **the near-term "Alpha" (what issue #1's title
literally names - "Fix Rollback Netcode for **Alpha**") is `Project-Plus-Dolphin`
+ stock Dolphin Netplay UI (host code, no custom matchmaking, no custom
menus), and is explicitly meant to become a PR into `Project-Plus-Dolphin`
itself.** That's the opposite framing from the "Project-Plus-Dolphin is a
dead end, ignore it" conclusion two sections down would suggest in isolation.
Under this reading: `project-plus-fork` (this repo) /
`Project-Plus-Dolphin:rollback`+`linux-fixes` (the "dumpall" rewrite,
2026-07-27 section) isn't a side experiment — **it may be the team's actual
current Alpha-track priority**, and issue #1 may be correctly filed after
all (just against an unfinished, WIP codebase - "theoretically functional...
TODO general code cleanliness, testing" per its own commit messages).

Meanwhile `savestate-efficiency` (this repo, what the user's fork and all of
this session's work - Stadium fix, #74, #72 - is built on) + its real
Dolphin pair `Brawlback-Team/dolphin` has a **complete, working-looking**
custom matchmaking system (ENet, a real server at `lylat.gg`, Ranked/
Unranked/Direct/Teams modes) - which sounds like substantial **Full release**
scope work, already built. `savestate-efficiency`'s ASM code already sends
`CMD_FIND_OPPONENT`/matchmaking-specific commands, which wouldn't make sense
if it were only ever meant to pair with plain stock Netplay.

**One plausible reconciliation** (not confirmed, just the most consistent
story available from what's readable in these repos): the team built the
full custom-matchmaking system first (`savestate-efficiency` +
`Brawlback-Team/dolphin`), then decided to ship something simpler and sooner
(FAQ: "to get the netcode out faster") by stripping back to just
rollback-on-stock-netplay for a first Alpha, which is the `project-plus-fork`
/ `Project-Plus-Dolphin:rollback` line — making `savestate-efficiency` +
`Brawlback-Team/dolphin` the **earlier, more feature-complete, but currently
secondary/paused** line, and `project-plus-fork` the **current near-term
priority**, even though it's less finished. This is a plausible reading of
the evidence, not a confirmed fact — **this needs a real answer from the
user or the Brawlback Discord, not more code archaeology.** Flagging clearly
rather than picking one silently: the user's 2026-07-27 answer to "keep
building on savestate-efficiency vs. track the dumpall rewrite" was made
*before* this FAQ was found, so it's worth re-asking with this context if
they have a spare moment, or asking in the Brawlback Discord
(`discord.gg/dzYRN32k4D`, linked from `GHIDRA.md`) directly.

**What this session did in the meantime**: kept working on `savestate-efficiency`
(all of today's commits below are on it) since it's the safer, most mature,
already-partially-verified line regardless of which one the team currently
prioritizes, and since switching now would orphan the Stadium/#74/#72 work
already done and build-verified there. If the user (or Discord) confirms
`project-plus-fork` is actually the priority, that work isn't wasted -
`savestate-efficiency` is still a real, functioning line worth having fixed
- it just means *future* sessions should shift focus there.

**UPDATE, same session, later**: found strong evidence this IS resolved, in
favor of `savestate-efficiency` + `Brawlback-Team/dolphin` being the real,
live, currently-deployed system, and the wiki FAQ simply being stale:

- `lylat.gg` is a real, live site (HTTP 200, confirmed), run by a *separate*
  community project (`project-lylat` on GitHub) that hosts matchmaking for
  multiple Slippi-ENet-compatible mods, not just Brawlback. Its `/download`
  page lists a real, versioned build: **"Lylat Dolphin" v5.0, for Super Smash
  Bros. Brawl**, with Linux/Windows/macOS downloads, plus setup instructions
  crediited to "the Brawlback Team" for playing mods like P+.
- `Brawlback-Team/dolphin`'s `Matchmaking.cpp` hardcodes
  `MM_HOST_PROD = "lylat.gg"` - i.e. this exact codebase is written to talk
  to the exact live server that exists today. That's not something a dead or
  superseded line would have live-coded in.
- An official `@BrawlbackTeam` tweet (old - Twitter snowflake ID places it
  around 2022, so this has been running for years, not new) announced
  `@ProjectLylat` "officially released the Brawl matchmaking build to the
  public... beta for Brawl and Project+ matchmaking is now OPEN for
  everyone... No paywalls. No restrictions." This is long-standing,
  established infrastructure, not a stale plan.

Best current read: the wiki's `FAQ.md` "Alpha vs Full release" framing
predates (or was simply never updated after) the matchmaking beta going
public, and reality has since moved past that "stock netplay only" Alpha
description into the real matchmaking system this session has been building
on all along. **`savestate-efficiency` + `Brawlback-Team/dolphin` is the
correct line to keep building on** — not because the earlier ambiguity was
wrong to flag, but because it's now resolved with real evidence. Didn't
confirm the *exact* relationship between `Brawlback-Team/dolphin` and
whatever `project-lylat` actually deploys as "Lylat Dolphin" (could be a
direct build of it, or a further downstream fork - `project-lylat/dolphin`
exists on GitHub too, forked directly from `dolphin-emu/dolphin`, default
branch `lylat/dolphin-matchmaking`, not yet compared line-by-line against
`Brawlback-Team/dolphin`) - if a future session wants to fully close that
loop, that's where to look next. Didn't chase it further today because the
practical answer (which line to build on) is already clear enough to act on.

### The actual pairing (for the `savestate-efficiency` line): `Brawlback-Team/dolphin`, not `Project-Plus-Dolphin`

`Brawlback-Team/Project-Plus-Dolphin` has **zero** Brawlback-related code on
its `master` branch, and the only Brawlback EXI implementation anywhere in it
(`rollback`/`linux-fixes` branches) speaks the *new*, incompatible
"dumpall"-pivot protocol documented below (2026-07-27 section) — confirmed
again today. What went unnoticed until today: **the Brawlback-Team GitHub org
has a separate repo, `Brawlback-Team/dolphin`**, not listed anywhere in this
doc's repo map before now, with a branch called `savestates-efficiency-v2`.
Its `EXICommand` enum
(`Source/Core/Core/Brawlback/BrawlbackUtility.h`) matches this repo's
`Brawlback-Online/include/exi_packet.h` **exactly**, byte for byte
(`CMD_FIND_OPPONENT=5`, `CMD_START_MATCH=13`, etc.) — this is the real,
correct Dolphin-side counterpart to `savestate-efficiency`. Also worth
knowing: the org has two more repos not previously in this doc's map —
`brawlback-common` (the shared EXI-struct submodule, already known, just
confirming the name) and `brawlback-wiki` (real docs — `GETTING_STARTED.md`,
`ROLLBACK.md`, `REPLAYS.md`, `GHIDRA.md`, `FAQ.md` — read `GHIDRA.md` if you
have Ghidra + Discord access, see below).

**Found via**: the org's GitHub profile page lists all 8 repos with one-line
descriptions - a 30-second check that should have happened before yesterday's
deep dive into Project-Plus-Dolphin. If a repo map in this doc ever again
claims "X is the Y fork" without that having been checked against the actual
org repo list, don't trust it blindly - re-verify.

**This resolves yesterday's "no matching Dolphin implementation" blocker on
issue #72 entirely** — see below. It also means the `IsInRollbackMode`/
`m_rollback_mode` "whole-session flag" hypothesis for issue #1 in the
"Brawlback's open issues" section further down was based on reading
Project-Plus-Dolphin's `NetPlayClient.h` — **the wrong repo's code.**
`Brawlback-Team/dolphin` has no `IsInRollbackMode` anywhere. That whole
hypothesis is superseded by the real one below.

**Status of forking it**: skanderbm123 does not yet have a fork of
`Brawlback-Team/dolphin`. This session's tooling can only fork/push to repos
already in its configured scope (`skanderbm123/brawlback-asm`, `Ishiiruka`,
`slippi-ssbm-asm`, `slippi-ssbm-c`, `Project-Plus-Dolphin`) and adding a
repo from a different owner (`Brawlback-Team`) mid-session isn't supported
once other sources are attached. **Asked the user 2026-07-28; they chose to
fork it themselves** (github.com/Brawlback-Team/dolphin → Fork button) rather
than have this session create a plain non-fork copy. If you're a fresh
session and `skanderbm123/dolphin` exists now, add it, clone
`savestates-efficiency-v2`, and apply the Dolphin-side fix described below
under issue #72 — it's small and exact, no need to re-derive it.

### Issue #1 ("cursors desync at menus") — real root cause found, not fixed

Filed against `Project-Plus-Dolphin`, almost certainly misfiled (an easy
mistake — this session made the identical one at first). The actual netcode
this bug would live in is `Brawlback-Team/dolphin` + this repo
(`brawlback-asm`), both cloned/available. Investigated for real this time,
using actual code instead of guesswork:

**In `brawlback-asm`'s `Rollback_Hooks.cpp`, `updateLowHook()` is the *only*
place local pad input gets redirected/relayed for netplay purposes, and it's
entirely gated behind `if (Netplay::IsInMatch())`:**
```cpp
void updateLowHook() {
    Utils::SaveRegs();
    if(Netplay::IsInMatch()) {
        memmove(&FrameLogic::inputBuffer, &g_gfPadSystem->m_gameGcnPads[Netplay::getGameSettings().localPlayerPort], sizeof(FrameLogic::inputBuffer));
        Util::InjectBrawlbackPadToPadStatus(&g_gfPadSystem->m_gameGcnPads[Netplay::getGameSettings().localPlayerPort], BrawlbackPad(), Netplay::getGameSettings().localPlayerPort);
    }
    Utils::RestoreRegs();
}
```
Right above it, `fixPadInconsistency()` explicitly calls the game's own
native `g_gfPadSystem->updateLow()` **only when NOT in a match** — i.e. the
code deliberately lets menus run on raw, un-relayed native pad input.
**Grepped the whole repo for any CSS/menu-specific pad-relay hook — there
is none.** Menu-phase input is never synchronized between the two clients at
all, by design (or by an acknowledged gap — the comments don't say which).

**On the Dolphin side, `Brawlback-Team/dolphin`'s `EXIBrawlback.cpp`,
`ProcessGameSettings()` has an explicit, maintainer-acknowledged shortcut:**
```cpp
// TODO: again assign proper stuff based on reality and since we really don't know what the player
// port is right now since netplay menu is not being used for this atm, we are going to assume
// that always p1 vs p1 are getting connected when netlay menu is set, get player port from game.
mergedGameSettings.localPlayerPort = 0;  // p1
```
This is set unconditionally — **both host and joining client get
`localPlayerPort = 0`**, not one 0 and one 1. `localPlayerPort` is the array
index brawlback-asm uses into `g_gfPadSystem->m_gameGcnPads[...]` (grepped -
this is its only real consumer) to decide whose physical controller counts
as "the local player" for rollback purposes.

**Combined hypothesis**: menu/CSS-phase cursor movement is never relayed
between clients (only gated-behind-`IsInMatch()` code exists for that), so
each client only ever renders its own local input — if Brawl's CSS scene is
still running in its native *local 2-player* mode (both P1 and P2 cursors
visible and driven by whatever's plugged into the local machine's controller
ports), each client would show a different, uncoordinated picture of "where
is the other player's cursor," which matches the reported symptom exactly.
Slippi's own real, working precedent for online CSS
(`slippi-ssbm-asm/Online/Menus/CSS/HandleInputsOnCSS.asm`, cloned in full at
`/workspace/slippi-ssbm-asm`) reinforces this: it does **not** synchronize
live cursor position at all — each player makes their pick entirely locally,
and only the final locked-in choice (`FN_TX_LOCK_IN`, sending char/color/stage
once) goes over the network. If Brawlback's CSS hasn't been adapted away from
showing two live, locally-driven cursors, that mismatch would look exactly
like "desync."

**Why this wasn't fixed today**: same category of blocker as issue #72's
text-entry widget. A real fix means hooking Brawl's actual CSS scene code to
either (a) relay cursor state like `updateLowHook` does for gameplay, or (b)
adapt the scene to a single-local-cursor model like Slippi's. Either way
needs Brawl-specific CSS memory offsets/struct layout that aren't in
BrawlHeaders and aren't safe to guess (Slippi's own fix uses Melee-specific
r13-relative offsets like `-0x49a7(r13)` that don't transfer to Brawl at
all). **Needs the `brawl` decomp repo or the Ghidra OpenBrawl-CBM shared
project (`brawlback-wiki/GHIDRA.md` — needs the actual Ghidra desktop app +
a Discord invite for credentials, not available in this sandboxed session)**
before real hook code can be written safely.

Checked 2026-07-28 (later) whether any *public* resource could substitute for
those two - it can't, at least not yet: `doldecomp/brawl` (the real,
public, community decompilation project - not the user's private `brawl`
repo, a different thing, cloned read-only at `/workspace/doldecomp-brawl`)
is only ~1% decompiled overall and has no menu/CSS/name-entry code at all
(`grep`'d both the source tree and the `sora_menu_sel_char` module's
`symbols.txt` - the latter has `muSelCharPlayerArea`'s already-known methods
like `setCharPic`, matching BrawlHeaders exactly, but nothing for
`MuSelctChrNameEntry`). `Sammi-Husky/BrawlModules` (cloned read-only at
`/workspace/BrawlModules`, the repo issue #70 itself suggests as a
starting point) only has stage (`st_*`) and one fighter (`ft_marth`)
reimplementations, no menu work at all - issue #70 is asking for genuinely
unstarted work, not pointing at something half-done. Don't re-check these
two in a future session expecting a different answer unless their own
upstream progress has visibly moved - this is a real, current dead end, not
a "didn't look hard enough" one. This is a solid, evidence-backed
diagnosis to hand to whoever has that access next, not a guess.

### Issue #72 — corrected, and the remaining fix is now exact and tiny

Yesterday's `CMD_DIRECT_CONNECT` was invented against the wrong repo and has
been **removed** (commit `3ee5792` today, brawlback-asm). The real protocol
was hiding in plain sight in `Brawlback-Team/dolphin`'s
`EXIBrawlback.cpp`, `handleFindMatch()`:
```cpp
Matchmaking::MatchSearchSettings search;
std::string connectCode;

// TODO: uncomment these lines when payload includes the actual mode and connect codes
#ifdef REMOVE_THIS_WHEN_PAYLOAD_IS_SET
search.mode = (SlippiMatchmaking::OnlinePlayMode)payload[0];
std::string shiftJisCode;
shiftJisCode.insert(shiftJisCode.begin(), &payload[1], &payload[1] + 18);
shiftJisCode.erase(std::find(shiftJisCode.begin(), shiftJisCode.end(), 0x00), shiftJisCode.end());
connectCode = shiftJisCode;
#else
search.mode = Matchmaking::OnlinePlayMode::UNRANKED;
#endif
```
`REMOVE_THIS_WHEN_PAYLOAD_IS_SET` is `#define`d nowhere in the codebase
(checked with grep) — this isn't a design decision, it's a TODO literally
waiting for the ASM side to send the right payload, which is now done
(`brawlback-asm` commit `3ee5792`): `setNextAnyOkirakuCaseFive()` now sends
`CMD_FIND_OPPONENT` with a 19-byte payload (mode byte matching
`Matchmaking::OnlinePlayMode` exactly, then an 18-byte connect code),
defaulting to `UNRANKED`/all-zero so nothing changes until something calls
`NetMenu::SubmitDirectConnectCode()`.

**The Dolphin-side fix is now applied** (`/workspace/brawlback-team-dolphin`
commit `211f5de`, on top of `savestates-efficiency-v2`, local only pending
`skanderbm123/dolphin`): removed the `#ifdef REMOVE_THIS_WHEN_PAYLOAD_IS_SET`
/ `#else` / `#endif` and kept only the "then" branch unconditionally, and
fixed the cast type — the original commented-out line said
`(SlippiMatchmaking::OnlinePlayMode)payload[0]`, a copy-paste leftover from
porting Slippi's own code; the type actually in scope is
`Matchmaking::OnlinePlayMode` (`Netplay/Matchmaking.h:32`), whose enum values
(`RANKED=0, UNRANKED=1, DIRECT=2, TEAMS=3`) were confirmed to match
brawlback-asm's `NetMenu::OnlinePlayMode` byte-for-byte. `<algorithm>` was
already included for `std::find`, so no new includes needed. Not build-tested
(no toolchain for full Dolphin in this session) but the change is a pure
type-fix + dead-branch removal against code already proven correct by the
disabled branch itself.

With this, **issue #72's protocol plumbing is fully closed on both ends**:
ASM sends the real mode+code payload (`brawlback-asm` `3ee5792`), Dolphin now
parses it instead of forcing `UNRANKED` (`dolphin` `211f5de`).

**What's still genuinely blocked after that fix lands**: nothing calls
`SubmitDirectConnectCode` with a real player-typed code yet, because reading
text out of Brawl's CSS name-entry widget still needs the same
offsets-unknown blocker as before (`MuSelctChrNameEntry`, opaque 0x94-byte
struct in BrawlHeaders). Slippi's real precedent
(`HandleInputsOnCSS.asm`'s `FN_LOAD_CODE_ENTRY`) confirms Melee reuses its own
nametag-entry widget for this exact purpose, setting a
`NAME_ENTRY_MODE` flag and a lock-in callback — strong confirmation this is
the right general shape for Brawl too, just needs Brawl's own offsets
(Ghidra or `brawl` decomp repo, same as issue #1 above).

All 5 forks were checked against their upstreams this session
(`git fetch upstream`, compare branches — no merges pushed upstream, per the
constraint above):

- **brawlback-asm**: our working branch is based on `savestate-efficiency`,
  which is still exactly in sync with `upstream/savestate-efficiency` (0
  commits behind). Nothing to pull there.
- **Project-Plus-Dolphin**: fork only had `master` (in sync with upstream).
  The `rollback` branch this repo map calls "the relevant one" **did not
  exist on the fork at all** — created it locally tracking
  `upstream/rollback` and pushed it to the fork, so `skanderbm123/Project-Plus-Dolphin`
  now actually has a `rollback` branch matching upstream's latest.
- **Ishiiruka**: fork's `slippi` branch exactly in sync with
  `upstream/slippi`. Nothing to pull.
- **slippi-ssbm-asm**: fork's `master` exactly in sync with upstream. Nothing
  to pull.
- **slippi-ssbm-c**: note upstream renamed its default branch from `master`
  to `main` at some point; our fork already tracks `main` correctly and is in
  sync. Nothing to pull.

### The big finding: upstream is mid-rewrite, and it's not merged anywhere yet

While fetching brawlback-asm's upstream, two branches turned up that are
**newer than `savestate-efficiency`** (which has been dormant since
2025-07-01): `linux-fixes` (last commit 2026-05-08) and `project-plus-fork`
(last commit 2026-06-15). Both are unmerged, both branch off the exact same
commit our fork's working branch does, and both are authored by Jared M.
White — the same person behind `BrawlHeaders` and the `savestate-efficiency`
line, i.e. this reads as the actual maintainer's own active work, not a
rando's experiment.

**What they contain**: starting from "Initial commit with, theoretically,
functional rollback" (Dec 2025) through "Lock rollback behind Windows and
only while in netplay" (Dec 2025) to "**Pivot to dumpall approach**" (May
2026) — this last commit adds `fast_codec.h` and `lz77.h` (LZ77
compression) and rewrites large parts of `Rollback_Hooks.cpp`. Read
literally, this is a shift from the current architecture (selective
`relevantHeaps` memory-region tracking for save-states) to **dumping the
entire game state wholesale and LZ77-compressing it for network transfer**.
After that shared point, the two branches diverge: `linux-fixes` adds one
more commit ("fix header case" — **the same case-sensitivity bug this
session independently found and fixed**, good independent confirmation both
fixes are correct), while `project-plus-fork` continues with 4 opaque
"Checkin" commits of its own that `linux-fixes` doesn't have.

**The exact same pivot exists on the Project-Plus-Dolphin side**, on a
branch also called `linux-fixes` there, sibling to (not merged into) the
`rollback` branch — same author, same timeline, same "Pivot to dumpall
approach" / "Lock rollback behind Windows" commits, plus "Add gekkonet fork
as submodule" (GekkoNet is a known open-source rollback-netcode library —
worth knowing if you dig into this further). So this is a coordinated,
cross-repo rewrite in progress upstream, covering both the ASM injection
side and the Dolphin emulator side.

**Why this session didn't act on it further**: this is a large,
semantically opaque (many commits are just "Checkin"), *unfinished* (rollback
is explicitly locked to "Windows only" mid-pivot, meaning the new approach
wasn't yet confirmed working cross-platform even by its own author) rewrite
of the exact file (`Rollback_Hooks.cpp`) that this session's Stadium and
duplicate-costume fixes live in. Merging or rebasing onto it blindly risked
destroying verified, working, build-tested changes in exchange for unreviewed
WIP with no way to test either side in this environment. That's a call worth
making with the user's input, not unilaterally mid-session.

**Open decision for the user**: keep building on `savestate-efficiency` (the
stable base with the Stadium + duplicate-costume fixes now on it), or shift
future fork work to track `project-plus-fork` / `rollback`'s dumpall
direction instead (which, if it pans out, might make the Stadium fix
unnecessary — a full state dump would naturally capture stage-transformation
state without a special-case hook). Nothing has to be decided immediately;
just flagging it so it doesn't get discovered by surprise later. If asked to
pick without further input: the dumpall branches are unfinished per their own
"Windows only" lock, so continuing on `savestate-efficiency` for now and
periodically re-checking `project-plus-fork` for a real merge/release seems
lower-risk than adopting an admittedly-incomplete rewrite this session
couldn't test either.

### The user's stated end goal, and the launcher

2026-07-28, later in the same session: the user clarified their real target
is the full experience — **working rollback + a launcher**, Slippi-esque,
not just the netcode. This matches the "Full release" scope in
`brawlback-wiki/FAQ.md` (launcher, in-menu matchmaking, replays).

There's a fourth repo for this: `Brawlback-Team/brawlback-launcher`
(Electron + React + TypeScript, GPL-3.0, forked from Slippi's own launcher
at commit `15db4bd`). Cloned read-only at `/workspace/brawlback-launcher`
this session (not yet forked to `skanderbm123` — same cross-owner
add_repo/fork limitation as `Brawlback-Team/dolphin`, see above; ask the user
to fork this one too when next in touch, same as `dolphin`).

**State found**: almost entirely un-rebranded Slippi Launcher scaffolding.
Fixed the safe, unambiguous parts (commit `2269126`, **local-only in the
`/workspace/brawlback-launcher` clone — not pushed anywhere, no fork exists
yet**):
- `package.json`/`electron-builder.json`/`README.md`: name, productName,
  description, repository URL, author, appId, artifact names, protocol
  scheme, clone instructions - all still said "Slippi Launcher" /
  `project-slippi/slippi-launcher`.
- **Real bug, not just cosmetic**: `electron-builder.json`'s
  `publish.owner`/`repo` was `project-slippi`/`slippi-launcher` - as
  configured, a built app's auto-updater would check the *wrong, unrelated*
  repo's GitHub releases. Fixed to `Brawlback-Team`/`brawlback-launcher`.
- **Also fixed, unrelated to rebranding**: `dolphin.service.mock.ts`'s
  `MockDolphinClient` didn't match the real `DolphinService` interface
  (`launchNetplayDolphin` had a bogus `{bootToCss}` param nothing calls it
  with - checked every real call site; `setMod`/`downloadDefaultMod` were
  missing entirely, added at some point for Brawlback's mod-switching
  feature but the mock never got updated). `npx tsc --noEmit -p
  tsconfig.json` was NOT clean before this fix, IS clean after. Also fixed
  a genuinely broken `package.json` field (`devEngines.node: ">=12.x"` - an
  invalid format that made every `npm`/`npx` invocation error out entirely
  on this newer npm; removed, since `engines.node` already covers it).

**Deliberately NOT touched, and why**:
- `.slp` file association / "Slippi File Format" description
  (`electron-builder.json`) - Brawlback doesn't have its own replay format
  yet (FAQ.md: "still in the works"), so there's nothing confirmed to
  rename this to. Don't invent one.
- `install-related macOS path hardcodes "Slippi Dolphin.app"`
  (`installation.ts` line ~28) - left alone since the real Brawlback/Lylat
  Dolphin build's actual `.app` bundle name on macOS isn't confirmed (only
  checked the Linux/Windows zips directly; the macOS one is presumably
  named similarly but wasn't downloaded/inspected this session).
- `slippi.service.ts`'s separate GraphQL usage (user auth via Firebase,
  play-key validation, user renaming, `MUTATION_INIT_NETPLAY`) - this is
  Slippi's own accounts backend, a much bigger and less certain question
  than the Dolphin download turned out to be. `lylat.gg` itself has its own
  "Sign In/Register" on the website, which suggests accounts/auth might be
  handled entirely by Lylat's own web platform, with the launcher not
  needing an equivalent auth layer of its own at all - but that's a real
  guess, not confirmed. Didn't touch this; needs a team/Discord answer on
  whether the launcher needs *any* accounts integration, and if so, what it
  should actually call.

**UPDATE, same session, later: the Dolphin-download gap above is now
actually fixed** (commit `f2f6cad` in the local launcher clone), not just
scoped. Confirmed via `lylat.gg/download` that the real download links for
Brawl are `https://github.com/project-lylat/dolphin/releases/download/5.0/LylatDolphin-{linux,windows,macos}.{zip}`
(exact URLs, fetched directly, not inferred). Rewrote
`fetchLatestVersion.ts` to hit GitHub's stable `releases/latest/download/<asset>`
alias (always redirects to whatever's actually current, so these links never
need manual updates) instead of the dead `SLIPPI_GRAPHQL_ENDPOINT` call, and
recovers the version number by reading the redirect's `Location` header
rather than needing any API access. **Verified for real, not just
typechecked** - ran it against the live network with `ts-node` and it
correctly resolved version `"5.0"` and working download URLs for all three
platforms, right now. Also fixed a real bug this exposed:
`installation.ts`'s `_isOutOfDate` called `semver.lt()` directly on values
like `"5.0"` and Dolphin's own `"5.0-19870"`-style `--version` output -
neither is valid strict semver, so `semver.lt` throws rather than compares.
Never reached before since the old GraphQL call never returned real data to
begin with. Fixed with `semver.coerce()` on both sides first.

This means the launcher's core "download and keep Dolphin up to date" loop
is now wired to something real. What's still missing for a genuinely
functional launcher: the accounts/auth question above, actual UI testing
(this sandboxed session has no display - could `yarn install` and typecheck,
never actually opened the Electron window), and likely more Slippi-specific
assumptions deeper in the Dolphin config/settings code
(`config/config.ts`'s `setSlippiSettings`, etc.) not yet audited.

**UPDATE, same session, later still**: swept for more hardcoded
`slippi.gg`/`project-slippi` references actually live in runtime code (not
just docs/config) and fixed what was safely fixable (commit `b0da467`,
local-only, same as everything else in this section):
- `common/constants.ts`: `slippiHomepage` → `lylat.gg`, `socials.twitterId`
  → `BrawlbackTeam`, `socials.discordUrl` → `discord.gg/dzYRN32k4D` - all
  three verified against real, confirmed sources this session (the org's
  actual Twitter link, the wiki's Discord invite), not guessed.
  `socials.patreonUrl` had no confirmed Brawlback-specific equivalent to
  point to (lylat.gg does have a donations page, but it's an in-page route,
  `/donations/new.js`, not cleanly external-linkable) - pointed at the
  Lylat homepage instead and **removed the Patreon-branded icon/copy in
  `Footer.tsx`** (keeping a real logo on a link that isn't actually a
  Patreon link would be actively misleading, worse than just
  under-branded).
- `BuildInfo.tsx`'s commit-hash link pointed at
  `project-slippi/slippi-launcher` instead of this repo.
- `SavedConnectionItem.tsx`'s outdated-Nintendont warning told users to
  download from "the Slippi website" - replaced with an honest note that
  Brawlback's console mirroring isn't built yet, and that Nintendont
  (GameCube-specific) likely isn't even the right tool for a Wii game like
  Brawl - rather than a broken/irrelevant link.

`npx tsc --noEmit` and `yarn lint` both clean (0 errors - lint had exactly
one, a prettier formatting nit in the line this session added, fixed via
`eslint --fix`; all remaining warnings are pre-existing, in files untouched
this session).

**Deliberately left alone in this pass** (real, known gaps, not
oversights): the broader Console/Nintendont mirroring feature area - bigger
scope, Wii vs. GameCube homebrew loader is a genuine unresolved design
question, not just a naming fix.

**UPDATE, 2026-07-29: `newsFeed.ts` fixed too** (commit `962d83e`).
Repointed the GitHub-release half of the feed at
`Brawlback-Team/brawlback-launcher` and `project-lylat/dolphin` (both real,
already-confirmed-live repos - not a guess), and removed the Medium blog
fetch entirely rather than invent a Brawlback publication slug that isn't
confirmed to exist. Better to show nothing than fabricate a source.

**UPDATE, 2026-07-29: one more pass, more real UI text bugs found and fixed**
(commit `7968663`). Swept `renderer/` more broadly for user-visible "Slippi"
strings: `IsoSelectionStep.tsx`'s invalid-ISO error told users to provide an
"NTSC 1.02" ISO (Melee's version identifier) "for Slippi Online" - this one
is a genuinely confusing bug, not just under-branding, since `verifyIso.ts`'s
actual hash-checking logic *is* already correctly Brawl-specific (real,
named Brawl 1.01 ISO hashes; Melee's own hash is explicitly listed as
`INVALID`) - the code was already right, only the error message shown to
users was wrong. Also fixed two native file-picker filters still labeled
"Slippi Dolphin", `UserInfo.tsx`'s "Slippi server error", and two more
descriptions/labels in Settings (`AdvancedAppSettings.tsx`,
`SupportBox.tsx`). Also removed an unverifiable "files streamed from
Slippi.gg" claim rather than guessing what to replace it with. `npx tsc
--noEmit` and `yarn lint` both clean throughout.

**UPDATE, 2026-07-29 (continued): a real, visible bug found and fixed**
(commit `9366078`) — **two of the four Settings tabs rendered a
completely empty box.** `App.tsx` routes `settings/*` to
`pages/settings/SettingsPage.tsx`, a 4-tab (General/Mods/Netplay/Replays)
layout whose `tabSwitch()` only had cases for tabs 0 and 1 - clicking
"Netplay" or "Replays" showed nothing. Found by comparing against
`containers/Settings/index.tsx`, which has a *complete*, correctly-wired
settings config (Game/Replays/Netplay/Playback/Advanced/Help - a superset)
consumed by `views/SettingsView.tsx` - except **`SettingsView` is never
imported or rendered anywhere in the app.** It's a fully-correct,
complete implementation that's 100% dead code, while the broken,
half-finished `SettingsPage` is what's actually live and routed to.

Completed `SettingsPage` rather than reverting to the dead `SettingsView`:
`SettingsPage`'s simpler 4-tab structure (one combined "Netplay" tab, no
separate "Playback" tab like `SettingsView` has) matches Brawlback's
single-Dolphin-build model (`settings/types.ts` has a comment: "brawlback
uses single dolphin build for netplay and replays") better than
`SettingsView`'s older Slippi-style split - so `SettingsPage` reads as the
intentional, Brawlback-adapted redesign, just abandoned partway through
rather than the wrong direction entirely. Wired "Netplay" to
`DolphinSettings(NETPLAY)` and "Replays" to `ReplayOptions`, both
already-correct, already-existing components that just weren't reachable
from anywhere. Also fixed two more leftover "Melee"-labeled strings in
`MeleeOptions.tsx` (file-picker filter, a radio label) inconsistent with
the rest of that same component, which already correctly says "Brawl ISO
File" elsewhere. `npx tsc --noEmit` and `yarn lint` both clean.

If a future session has time: `views/SettingsView.tsx` and the unused half
of `containers/Settings/index.tsx`'s config (the separate "Playback"
Dolphin-settings entry) are now confirmed genuinely dead code - safe to
delete once someone's comfortable that `SettingsPage` really is the
intended direction (this session left them in place rather than deleting
working, if unreachable, code without being asked to).

**Found a much bigger version of the exact same dead-code pattern -
deliberately NOT fixed, unlike the Settings one, and here's why.**
`App.tsx`'s live routing has exactly 3 routes: Home, Settings (now fixed
above), and `replays/*` → a two-line stub component (`function Replay() {
return <h3>Replay</h3>; }` - literally just that text, no functionality at
all). Meanwhile `views/MainView.tsx` - like `SettingsView` before it, never
imported or rendered *anywhere* in the app, 100% dead code - has a
complete, real top-level shell: a proper `Header` with a menu, and four
fully-implemented pages wired up correctly: Home, **Replays** (→ the real
`ReplayBrowserPage`, a whole feature with its own hooks/components), 
**Spectate** (→ `SpectatePage`, gated behind `AuthGuard` - real broadcast
UI with sub-components for sharing gameplay), and **Console Mirror** (→
`Console`, the Nintendont-based real-hardware mirroring feature from
earlier in this doc).

**Why this one wasn't just "completed" the way Settings was**: unlike
`SettingsPage` (where the reduced structure had a specific, confirmed
Brawlback-adaptation reason - the single-Dolphin-build comment), the
missing pieces here have a *different*, equally-confirmed reason to
believe the live app's reduced surface is intentional, not accidental:
`brawlback-wiki/FAQ.md` explicitly says **both** replays and console
mirroring are "still in the works" for Brawlback. Spectate needs real
accounts (`AuthGuard`), which - per the still-unresolved auth question
elsewhere in this doc - may not even be a Brawlback launcher concern at
all (Lylat's own website may handle that). Wiring in a fully-built replay
browser for a replay *format that doesn't exist yet*, or a spectate/console
UI that depends on backend pieces this session already flagged as
unconfirmed, risks replacing an honest "not built yet" placeholder with a
broken-in-a-different-way UI that looks finished but silently does nothing
useful (or worse, errors trying to parse/find files in a format Brawlback
hasn't defined). **Left as-is, flagged clearly rather than guessed at** -
this needs someone who actually knows Brawlback's replay-format and
accounts status to decide whether `MainView` should be restored (once
those backend pieces are ready) or `Replay`'s stub should just get a
better "coming soon" message in the meantime.

**Found one more live (not dead-code) instance of the same leftover-Slippi-text
bug, in `ModsOptions.tsx` (commit `e4ed2e8`).** This tab is actually rendered
(`SettingsPage.tsx` case 1), so unlike `MainView` above, this one needed fixing,
not just documenting. The ISO file-picker filter still said `"Melee ISO"` -
missed by the earlier text sweep (`7968663`) because that commit didn't touch
this file. Also fixed a real mislabel: the "Add Mod" dialog's SD-card-file
`<input type="file">` was labeled "Launcher Path", duplicating the label on
the ELF-file input right above it, which would mislead anyone adding a mod's
SD card file. Both one-line text fixes, typecheck-clean.

**Everything in this whole launcher section is sitting as local git commits
in `/workspace/brawlback-launcher` (currently: `2269126`, `f2f6cad`,
`b0da467`, `7968663`, `962d83e`, `9366078`, `e4ed2e8`, in that order on top of
the real `Brawlback-Team/brawlback-launcher` history) — none of it is pushed
anywhere, because no `skanderbm123` fork of `brawlback-launcher` exists
yet.** Same situation as `Brawlback-Team/dolphin` above: ask the user to
fork `Brawlback-Team/brawlback-launcher` too, next time you're in touch,
then push this branch there.

**Build/verify setup for next time**: `cd
/workspace/brawlback-launcher && yarn install` (took ~65s, works cleanly on
Node 22 despite the repo listing `engines.node >=12` - only real
compatibility issue found was the `devEngines` bug above, now fixed).
`npx tsc --noEmit -p tsconfig.json` for a typecheck (couldn't run the actual
Electron GUI in this sandboxed, display-less environment, so this is as far
as verification went - no runtime testing of the app itself).

### 2026-07-29: the flagged ProcessGameSettings lead, resolved - and it caught a mistake

Went back to actually verify the lead below instead of leaving it flagged,
and it turned up something important: **the earlier #73 "fix" (commit
`cbfdc87`) was wrong and has been reverted** (dolphin commit `8035041`).
Recording the full corrected trace here since the wrong version was written
up with high confidence below and in the punch-list - anyone reading old
context needs the correction, not just the new conclusion.

First, the double-invocation question this section originally raised is
**settled: there is no double invocation**. `ProcessGameSettings` has
exactly one call site (`ProcessNetReceive`'s `CMD_GAME_SETTINGS` case), and
tracing the actual packet flow shows each side receives exactly one
`CMD_GAME_SETTINGS` packet for the whole match: host broadcasts its raw
settings once right after connecting (`NetplayThreadFunc`, right after the
connect handshake); client receives that, merges, and broadcasts its own
merged result back *once* (end of the `!isHost` branch); host receives
*that* once. Two packets total, one `ProcessGameSettings` call per side -
never two calls on the same side.

But tracing that flow all the way through is exactly what exposes the real
bug. `BroadcastGameSettings` (confirmed in `Netplay.cpp`) serializes
whatever `GameSettings*` it's given verbatim - no rearranging. So:

- **What the host broadcasts** (`&this->gameSettings`, raw, straight from
  `handleStartMatch`'s memcpy of the ASM side's `CMD_START_MATCH` payload)
  really does have the host's own data at `playerSettings[0]` and nothing
  meaningful at `[1]` - this is the fact `fillOutGameSettings` guarantees
  (it only ever writes index `[0]`, regardless of host/client role).
  **This is the packet the client receives**, and the client's `!isHost`
  branch correctly reads the host's data from `opponentGameSettings->playerSettings[0]`.
- **What the client broadcasts back** is *not* that same kind of raw
  packet - it's `&mergedGameSettings`, the result of the client's own merge,
  taken at the end of the `!isHost` branch after it has already: copied its
  own raw data (originally at its own `[0]`) into `[1]` (client occupies the
  P2/`localPlayerIdx=1` slot), then overwritten `[0]` with the host's data
  it just received. **This is the packet the host receives** as
  `opponentGameSettings` in its own (host, `isHost` branch) call - and by
  this point index `[0]` holds the *host's own data echoed back*, while
  index `[1]` holds the *client's real choice*.

`cbfdc87` changed the host's read from `opponentGameSettings->playerSettings[1]`
(client's real data - correct) to `[0]` (host's own echoed data - wrong),
reasoning from the `fillOutGameSettings`-always-writes-`[0]` fact - which is
true, but doesn't apply here because the host isn't reading the client's
raw `CMD_START_MATCH` payload, it's reading the client's *own merge output*,
which has already moved that data to `[1]`. That fix would have made both
players load in with the host's costume instead of syncing the client's
real choice - the opposite of the intended fix, and worse than the
original bug in some ways (P1 was already fine; this would have broken P1's
display too on the host's own screen, not just P2's).

**Net result: reverted to the original `playerSettings[1]` read (dolphin
commit `8035041`), and issue #73 goes back to unresolved.** The original
bug report ("costumes not syncing... P2 loading in as the first secret
costume") still needs a real root cause - it is not this index, since the
index was already correct. Worth checking next: whether `this->gameSettings`
on the host is actually still holding the *host's own* raw data by the time
the client's rebroadcast arrives (i.e. no unexpected overwrite in between
those two network events), and whether the ASM-side `MergeGameSettingsIntoGame`
consumption of `playerSettings[0]`/`[1]` as P1/P2 lines up with whatever
`localPlayerIdx`-based costume/CSP rendering code does elsewhere - that
combination wasn't checked this pass. Needs a live two-client test or
Ghidra-level ASM tracing to actually pin down, same as #1/#75/#76.

## The goal

The user's ultimate goal: a **ranked online mode for Brawl / Project M /
Project+ with Slippi-style rollback netcode**. Not a decompilation project,
not a native PC port — an emulator-netcode-engineering effort, same shape as
what Slippi did for Melee (fork Dolphin, add fast save-states + deterministic
resimulation + network input injection, running the original game).

Earlier ideas that were considered and explicitly rejected in favor of this:
- **Static recompilation / native PC port** (e.g. DolRecomp/ModernGekko style)
  — rejected because it doesn't serve the actual goal (ranked rollback play);
  it was a detour before the user clarified their real objective.
- **"Emulation has too much latency, I need real native"** — investigated and
  found to be false: modern Dolphin actually beats real GameCube/Wii hardware
  in input latency. So emulation-based rollback (Brawlback's approach) is not
  an authenticity compromise.

Given that, the user chose to **fork and contribute to Brawlback**
(github.com/Brawlback-Team), an already-active project doing exactly this,
rather than build rollback netcode from scratch — informed by comparing
against **Slippi's own open-source Melee implementation**, which solved the
identical problem years earlier and hit many of the same stage/character
determinism issues Brawlback is now hitting.

There is a **separate, unrelated** decomp project (`brawl` repo, dtk-based
matching decompilation of the actual Brawl source) that the user is also
running in parallel with another AI agent handling characters/stages in its
own PR. That project is NOT this one. It only intersects here because reading
the decompiled `st_stadium` source (see below) was useful reference material
for understanding retail addresses/struct layouts when writing the Stadium
fix in *this* repo. Do not confuse the two; don't touch the `brawl` repo's
stage/character files under the assumption it's "the same task."

## Repo map (all already forked to github.com/skanderbm123)

Cloned locally to `C:\Users\skand\OneDrive\Bureau\brawlback\` on the user's
machine (not accessible if you're a fresh remote/cloud session — clone fresh
from GitHub instead):

| Repo | Fork | Upstream | Branch | Role |
|---|---|---|---|---|
| **dolphin** | **skanderbm123/dolphin — does not exist yet, user is forking it (see 2026-07-28 section above)** | **Brawlback-Team/dolphin** | **`savestates-efficiency-v2`** | **THE REAL emulator fork that pairs with this repo's `savestate-efficiency` protocol.** Core files: `Source/Core/Core/HW/EXI/EXIBrawlback.cpp` + `Source/Core/Core/Brawlback/Netplay/Matchmaking.cpp` (Slippi-derived matchmaking client, talks to Brawlback's own server at `lylat.gg` over ENet - this is what "Lylat" in issue #72 refers to). Also has a dedicated Qt settings pane (`DolphinQt/Settings/BrawlbackPane.cpp`). |
| ~~Project-Plus-Dolphin~~ | skanderbm123/Project-Plus-Dolphin | Brawlback-Team/Project-Plus-Dolphin | `rollback` | **NOT the emulator fork that pairs with this repo (corrected 2026-07-28) — `master` has zero Brawlback code, and `rollback`/`linux-fixes` speak an incompatible, unrelated "dumpall"-rewrite protocol (see 2026-07-27 section). Kept synced in case that rewrite matures later, but don't treat it as "the" Dolphin fork.** |
| brawlback-asm | skanderbm123/brawlback-asm | Brawlback-Team/brawlback-asm | `savestate-efficiency` (default) | Syringe-injected ASM/C++ that runs *inside* the game via SD-card-loaded plugin. Core file: `Brawlback-Online/source/Rollback_Hooks.cpp` (2000+ lines) — game-side frame loop hooks, fixed RNG seed `0x496ffd00`, `relevantHeaps` save-state allowlist. **This is where the Stadium fix (below) was committed.** |
| Ishiiruka | skanderbm123/Ishiiruka | project-slippi/Ishiiruka | `slippi` (sparse-checked-out: `Source/Core/Core/HW`, `NetPlayClient.*`, `State.*`, `Slippi/`) | Slippi's Dolphin fork, for comparison. Core file: `Source/Core/Core/HW/EXI_DeviceSlippi.cpp` (3644 lines) — Slippi's equivalent of EXIBrawlback.cpp, much more mature/complete. |
| slippi-ssbm-asm | skanderbm123/slippi-ssbm-asm | project-slippi/slippi-ssbm-asm | default | Slippi's game-side ASM injection code (Melee equivalent of brawlback-asm). **`Online/Core/Hacks/Stadium/`** is the key directory for the Stadium fix precedent; **`Online/Menus/CSS/`** (esp. `HandleInputsOnCSS.asm`, `TextEntryScreen/`) is the key directory for issues #1 and #72. |
| slippi-ssbm-c | skanderbm123/slippi-ssbm-c | project-slippi/slippi-ssbm-c | default | m-ex-based C code for Slippi — turned out to be CSS/menu/ranked-mode UI code, not core netcode. Lower priority than the ASM repo; only skimmed, not deeply mined yet. |
| brawlback-wiki | not forked (read-only reference so far) | Brawlback-Team/brawlback-wiki | main | Real project docs: `GETTING_STARTED.md`, `ROLLBACK.md`, `REPLAYS.md`, `GHIDRA.md` (shared reverse-engineering server, needs Ghidra desktop app + Discord), `FAQ.md`. Worth a full read next session. |

## What's been done: the Stadium transformation freeze fix

**Status: written, pushed, and now BUILT successfully (2026-07-23). Still NOT
tested in an actual netplay match** — this remote session has no Dolphin
build, no Brawl ISO, and no second machine, and the user is out of the
country and unable to test either right now. See "Build results" below for
what was actually verified, and "To build/test this" for what's still needed
from a human with the game.

### The problem

Pokemon Stadium's dynamic terrain-transformation mechanic (picking a random
terrain type via RNG, loading/unloading background archives, changing
hitboxes) is a classic rollback desync source — non-deterministic-feeling
timing, heap alloc/free lifecycle that's hard to resimulate correctly under
rollback. **Slippi hit the exact same problem on Melee's Pokemon Stadium**
and solved it not by making the RNG/heap lifecycle resimulate correctly, but
by simply **making the transformation trigger permanently believe it's in
training mode (frozen)**, i.e. disabling the whole mechanic online rather
than fixing its determinism. See `slippi-ssbm-asm/Online/Core/Hacks/Stadium/`:
`IngameCheckIfFrozen.asm` (hooks `PokemonStadium_TransformationDecide`),
`CustomZeroBuffer.asm` (handles re-entrant buffer zeroing during rollback),
`GrPsxIsValid.asm` (validates the transformation archive loaded).

Brawlback had **zero** equivalent for Brawl's Stadium (confirmed via
exhaustive grep — no "stadium" hits anywhere in brawlback-asm before this
fix).

### The fix

Rather than porting all three Slippi hacks 1:1, I found a single, smaller
patch achieves the same effect for Brawl's version, by studying the actual
decompiled source (`brawl` repo, `src/mo_stage/st_stadium/st_stadium_update.cpp`
— a fully-decompiled, real-source file from the separate decomp project) and
disassembling the retail `st_stadium.rel` directly (using
`brawl/tools/disasm.py`, a small custom PowerPC disassembler already in that
repo) to confirm exact byte offsets.

`stStadium::update()` only queues a new terrain transformation when:
```cpp
if (m_event0.isReadyEnd()) {
    if (!m_event1.isEvent()) {
        // ... RNG pick via m_shuffleOrder, m_event1.start() ...
    }
}
```
`m_event1` is a `grTenganEvent` (state machine: `NoEvent=0, Running=1,
ReadyEnd=2`) living at offset `0x288` within `stStadium`, with its `m_state`
field at `+0xA4` within that (so `self + 0x32C` overall). The fix
(`Brawlback-Online/include/StageFixes.h` + `source/StageFixes.cpp`) hooks
`stStadium::update` at REL offset `0x27A8` (confirmed via disassembly — this
is the function's actual entry point) via
`api->syInlineHookRel(0x000027A8, StageFixes::FreezeStadiumTransform,
Modules::ST_STADIUM)` (module ID 59, confirmed from
`lib/BrawlHeaders/Brawl/Include/modules.h`). The hook callback forces
`m_event1.m_state = Running` whenever `Netplay::IsInMatch()` is true — this
makes the game's own `!m_event1.isEvent()` check permanently false, so the
whole transformation pipeline (RNG pick, archive alloc/free, camera events)
never runs again for the rest of an online match. No RNG determinism or
heap-lifecycle resimulation problem needs solving because the mechanic simply
never triggers — this is why it sidesteps the need for Slippi's other two
hacks (CustomZeroBuffer / GrPsxIsValid), which exist to handle transformation
lifecycle edge cases that this approach avoids entirely.

**How this was verified so far (and what's NOT verified):**
- ✅ Disassembly of the retail `.rel` was cross-checked byte-for-byte against
  the decompiled C++ source at that address — the logic match is solid.
- ✅ The hook API call (`syInlineHookRel` + module ID) follows the exact
  pattern used by dozens of other working hooks already in
  `Rollback_Hooks.cpp`'s `InstallHooks()`.
- ✅ **Compiles and links clean** (2026-07-23, this session — see "Build
  results" below). `FreezeStadiumTransform` confirmed present as an exported
  symbol in the final `Brawlback-Online.rel` via `nm`/`strings`.
- ❌ Still NOT run in Dolphin. NOT tested in an actual netplay match. Nobody
  in this session had Dolphin + a Brawl ISO available to go further than a
  compiler check.
- ❌ Haven't verified `stStadium::update`'s calling convention preserves `r3`
  (the `this` pointer) correctly through Syringe's inline-hook trampoline the
  way I assumed — this should hold (the trampoline only *saves* r3-r10 to the
  stack before branching to the hook, doesn't clobber them), but it's
  reasoning from reading the trampoline's assembly, not from a test run.
- ❌ Haven't verified whether `m_event0` (the outer timer) needs any special
  handling once `m_event1` never starts — reasoned through the source that
  `m_event0.update()` continues running harmlessly forever in the "ready but
  nothing happens" state, but this is inference, not observation.

### Build results (2026-07-23)

Built successfully end-to-end on a fresh Linux clone via
`python3 ./bbk.py setup && make` (exit code 0, both `Brawlback-Online.rel` and
`sy_core.rel` produced and copied into `sd-card/vBrawl/pf/{plugins,module}`).
Three build-system bugs had to be fixed along the way — all pre-existing,
none related to the Stadium fix's actual logic, all now fixed and pushed in
commit `28c54bd`:

1. **`Rollback_Hooks.cpp` included `"EXI_Hooks.h"`** but the file on disk is
   `EXI_hooks.h` (lowercase `h`). Silently fine on case-insensitive
   filesystems (Windows/macOS, what earlier dev machines presumably used),
   fatal `file not found` on Linux. Fixed the include to match the real
   filename.
2. **Both `Brawlback-Online/Makefile` and `lib/Syriinge/Makefile`** ran a
   separate `clang -MMD $< -o $(DEPSDIR)/$*.d` step (no `-c` flag) to
   generate Make dependency files. Without `-c`, clang compiles *and links*,
   so it silently wrote a full linked ELF blob to the `.d` path instead of a
   real dependency fragment. Invisible on a from-scratch build (no `.d` files
   exist yet to `-include`), but every subsequent incremental `make` failed
   with `missing separator` because `-include $(DEPENDS)` tried to parse an
   ELF binary as a Makefile. Fixed by folding dependency generation into the
   real compile command via `-MMD -MF`, the standard idiom — one invocation
   does both the object file and the correct `.d` file.
3. **Case-sensitivity bugs inside the `lib/BrawlHeaders` submodule itself**
   (a separate GitHub org's repo, `JaredWhiteOne/BrawlHeaders`, not ours to
   patch here): `revolution/FA.h` does `#include <revolution/fa/...>` but the
   real directory is `revolution/FA/` (and `FAremove.h` vs. real
   `FARemove.h`). Worked around **locally only** with two symlinks
   (`revolution/fa -> FA`, `FA/FAremove.h -> FARemove.h`) inside the
   submodule's working tree so the build could proceed — these are
   *untracked, not committed* (submodule dirty-content changes don't get
   swept up by a normal `git add` in the parent repo, confirmed via `git
   status`). **Anyone building fresh on a case-sensitive filesystem (Linux,
   and some macOS setups) will need to redo this symlink workaround
   themselves** until/unless it's fixed upstream in BrawlHeaders — not
   something to fix in brawlback-asm's own commits.

### To build/test this

```bash
cd brawlback-asm
python3 -m pip install --user -r tools/requirements.txt  # click, requests, rich
git submodule update --init --recursive   # lib/BrawlHeaders, lib/brawlback-common
python3 ./bbk.py setup   # downloads a prebuilt LLVM/clang fork + elf2rel
                          # binary from a Brawlback-maintained S3 bucket —
                          # see bbk.py for exact URLs if you want to audit it first
# On a case-sensitive filesystem (Linux, some macOS), also add:
#   ln -s FA lib/BrawlHeaders/OpenRVL/include/revolution/fa
#   ln -s FARemove.h lib/BrawlHeaders/OpenRVL/include/revolution/FA/FAremove.h
mkdir -p sd-card/vBrawl/pf/plugins sd-card/vBrawl/pf/module  # if not present
make
```
This now succeeds and produces `Brawlback-Online.rel` / `sy_core.rel`. Still
needed from here (needs a human with Dolphin + game files, or a differently
provisioned session): follow the repo's own `README.md` for getting the
output onto an SD card / loaded via `BRAWLBACK-ONLINE-DEV.elf` in Dolphin, and
test an actual Stadium match in netplay/rollback mode to confirm no crash and
no more visual transformation once a match starts.

## Brawlback's open issues (current priorities, checked 2026-07-22, build status updated 2026-07-23)

Ranked by what's most valuable to tackle next:

1. **[Project-Plus-Dolphin #1](https://github.com/Brawlback-Team/Project-Plus-Dolphin/issues/1)
   "Fix Rollback Netcode for Alpha"** — cursors desync at menus. **Likely
   misfiled against the wrong repo (see 2026-07-28 section at the top of this
   doc) — the real netcode lives in `Brawlback-Team/dolphin` +
   `brawlback-asm`.** ~~Working hypothesis from reading
   `NetPlayClient.h`/`.cpp`: `m_rollback_mode`/`IsInRollbackMode()`...~~ **That
   hypothesis was built from `Project-Plus-Dolphin`'s code and is superseded -
   `Brawlback-Team/dolphin` doesn't even have `IsInRollbackMode`.** The real,
   evidence-based root cause (found 2026-07-28, see top section): menu-phase
   pad input is never relayed between clients at all (`updateLowHook`, the
   only pad-relay code that exists, is entirely gated behind
   `Netplay::IsInMatch()`), compounded by `Brawlback-Team/dolphin`'s
   `ProcessGameSettings()` hardcoding `localPlayerPort = 0` for both host and
   client unconditionally (an explicit, commented TODO, not a guess). Still
   needs live two-client testing to *confirm*, and a real fix needs Brawl's
   CSS memory layout (Ghidra or the `brawl` decomp repo) which this session
   doesn't have - but the diagnosis itself is solid, not speculative anymore.
2. **The Stadium fix above** — code written AND now confirmed to build/link
   clean (2026-07-23). Only remaining step is an actual in-game netplay test,
   which needs a human with Dolphin + the ISO.
3. **[brawlback-asm #74](https://github.com/Brawlback-Team/brawlback-asm/issues/74)
   "Add Functionality for Duplicate Costumes"** — **DONE, commit `e750dba`,
   compiles clean, NOT tested in a live match.** Checked both `slippi-ssbm-c`
   (turned out to only have a `teamCostumeIndex` struct field — team-color
   stuff, not this) and `slippi-ssbm-asm` (searched the whole `Online/Menus/CSS/`
   tree — no dedicated "duplicate costume" hack exists there either; Slippi's
   actual fix for this, if it exists, isn't in either open-sourced repo, so
   there was nothing to port 1:1). Instead found that `Rollback_Hooks.cpp`
   already has the exact right insertion point: each client captures its own
   local player's character/costume/file-index in `fillOutGameSettings`,
   sends it over the network, and `MergeGameSettingsIntoGame` /
   `GMMelee::PopulateMatchSettings` merges both players' independently-made
   choices right before `FillInMeleeObj` writes them into
   `gmGlobalModeMelee`. That's exactly where a same-character-same-costume
   collision can be detected and is deterministic on both clients (required
   for rollback) since it's a pure function of the exchanged settings, no
   RNG involved.

   Implemented option 2 from the issue (auto-advance to next costume) rather
   than option 1 (Slippi-style lightening): lightening needs new
   palette/texture asset work that doesn't exist in this codebase, while
   auto-advance is a same-session data-only change using pipes already in
   place. On an exact collision (same `charChoices`, `costumeChoices`, AND
   `fileIndexChoices` for P1/P2), P2's costume is bumped via
   `(costumeChoices[1] + 1) % 4`. Verified via web search that every
   character in Brawl's base roster has **at least 4** alt costumes
   (Pikachu is the minimum, most have 6, a few have 5) — so mod-4 is
   guaranteed to land on a valid costume slot for literally any character,
   without needing a per-character costume-count table (which isn't
   available anywhere in `lib/BrawlHeaders` — checked). This is a
   deliberately conservative choice: it always works, but for characters
   with 5 or 6 costumes it never reaches for slots 4/5. Fine as a fix; if
   someone later wants full costume-count utilization, that needs a
   per-character table, which would need either upstream `BrawlHeaders`
   support or the same disassembly-based reverse-engineering approach used
   for the Stadium fix.

   **Not verified**: an actual live match with two clients picking the same
   character+costume, to confirm the CSP/costume actually renders correctly
   and nothing else downstream assumes P1/P2 costumes are always whatever
   was locally chosen (e.g. some other display/UI element reading the
   pre-adjustment costume index and showing a mismatch with the in-match
   model).
4. **[brawlback-asm #72](https://github.com/Brawlback-Team/brawlback-asm/issues/72)
   "Implement menuing for Brawlback direct connect"** — explicitly says
   "probably just follow what Slippi has," and that this "requires
   modifications to both the ASM code and Dolphin emulator," workflow TBD.
   **2026-07-27: investigated in depth, partially implemented (send-side
   plumbing only, commit `51ca4dc`), blocked on two confirmed dead ends for
   the rest.** Full account below since this took a lot of digging - useful
   context for anyone continuing it.

   **What currently exists**: `Rollback_Hooks.cpp`'s `NetMenu` namespace (see
   `Rollback_Hooks.h` around line 208) is **not** a direct-connect system —
   it's hooks that hijack Brawl's existing WFC "Anybody" quickplay flow
   (`connectToAnybodyAsyncHook`, `forceFriendCode`, `forceConnection`,
   `BBBootTosqNetAnyOkiraku`) to make the *game* instantly believe a WFC
   match was found, bypassing Nintendo's actual WFC servers. Tracing where
   the real network connection comes from (`CheckIsMatched` reads game
   settings straight off an EXI channel at a fixed frequency, no handshake
   logic in this repo at all) makes clear that **the actual peer-to-peer
   connection is established entirely outside the game**, via
   Project-Plus-Dolphin's own stock, unmodified Netplay Host/Join dialog
   (standard Dolphin functionality, using Dolphin's built-in traversal-server
   host codes — confirmed 8 characters, `NETPLAY_CODE_SIZE` in
   `Common/TraversalProto.h`). Today, players have to alt-tab to that
   separate Dolphin panel and type a host code there *before* booting the
   game; these ASM hooks then just fast-forward the game's own WFC UI once
   Dolphin's connection already exists. This issue is asking to let players
   type that code from inside Brawl's own menu instead.

   **What was implemented** (`Brawlback-Online/include/exi_packet.h`,
   `Rollback_Hooks.h`/`.cpp`): a new `CMD_DIRECT_CONNECT` EXI command and
   `NetMenu::SubmitDirectConnectCode(const char[8])`, following the exact
   `EXIPacket::CreateAndSend` pattern every other outbound command in this
   file already uses. Compiles clean, verified via `make`. This is real,
   correct, useful groundwork — but nothing calls it yet, and nothing
   confirmed to receive it exists yet either. Two blockers stopped this from
   going further:

   **Blocker 1 - no known-safe way to capture what a player types.** Slippi's
   real precedent (`slippi-ssbm-asm`, cloned in full at
   `/workspace/slippi-ssbm-asm` this session — add it back with `add_repo` if
   it's not already in a future session's scope): `Online/Menus/CSS/TextEntryScreen/`
   has a complete text-entry-screen implementation (`InitNameEntry.asm`,
   `OnEnterText.asm`, `OnConfirmButtonHandler.asm`, `AutoComplete.s`,
   `Display8Characters.asm`, etc.) built on top of Melee's own CSS
   nametag-entry widget. Brawl has the equivalent widget —
   `lib/BrawlHeaders/Brawl/Include/mu/selchar/mu_select_character_name_entry.h`,
   `class MuSelctChrNameEntry` — but BrawlHeaders only has its *size*
   (`char _0[0x94]`, an opaque blob), not a single named field. Reading back
   what a player actually typed into it would mean guessing byte offsets
   inside that 0x94-byte struct with zero reference material, which is a
   real memory-safety risk in actual gameplay (read the wrong offset, get
   garbage or crash), categorically different from an unverified build. The
   Stadium fix could hardcode offsets safely because the separate `brawl`
   decomp repo had a fully-decompiled, named-field version of
   `st_stadium_update.cpp` to check against; nothing equivalent exists here
   in what's accessible to this session. **Next step needs either the `brawl`
   decomp repo (out of scope for this session, see the goal section above)
   or live disassembly of the retail widget's methods** before any code can
   safely read the entered text.

   **Blocker 2 - couldn't find a Dolphin-side implementation of the protocol
   this ASM code actually speaks.** Checked all three of
   skanderbm123/Project-Plus-Dolphin's branches against Brawlback-Team's
   upstream (`master`, `rollback`, `linux-fixes`):
   - `master` has **zero** Brawlback-related files anywhere in its tree —
     not EXI_Brawlback.cpp, not anything. It's a plain Project+/Dolphin
     build.
   - The *only* place `EXI_Brawlback.cpp`/`.h` exist at all is on
     `rollback`/`linux-fixes` — and per the dumpall-pivot finding above,
     those speak a completely different, incompatible EXI command numbering
     (`CMD_END_FRAME=1`, `CMD_GET_REMOTE_INPUTS=5`, etc., all introduced
     starting from the Dec 2025 "Initial commit... theoretically functional
     rollback" - confirmed via `git log --follow` on that file, it has no
     history before that commit) than what this ASM code currently sends
     (`CMD_FIND_OPPONENT=5`, `CMD_START_MATCH=13`, etc. - confirmed these
     are live/called, not dead code, via grep in `Rollback_Hooks.cpp`).

   Put plainly: **as far as this session could determine, there is no
   Project-Plus-Dolphin implementation anywhere in the fork or upstream that
   matches the EXI protocol `savestate-efficiency`'s ASM code is currently
   built against.** This might mean the real counterpart lives somewhere not
   in this repo map (the user's local machine, a different branch/fork not
   yet pushed to GitHub, something not yet synced), or it might mean this
   pairing genuinely never existed in a complete, working state. Either way,
   this session couldn't verify it, and didn't want to write speculative
   Dolphin-side C++ (thread-safety-sensitive code, in an unfamiliar
   ~large codebase, completely unbuildable/untestable in this environment's
   remaining time budget) against a foundation it couldn't confirm. This is
   worth a direct question to the user rather than more unilateral digging:
   **does a working Dolphin-side counterpart to the current ASM protocol
   exist somewhere, and if so, where?**

   (For what it's worth, the general *shape* of a fix was scoped enough to
   be useful if blocker 2 resolves: Dolphin already has everything needed
   architecturally — `Core::QueueHostJob` for safe CPU-thread-to-UI-thread
   dispatch, the `Host_*` extern-function pattern in `Core/Host.h` /
   `DolphinQt/Host.cpp` for Core-to-frontend calls without a reverse Core→Qt
   dependency, and `MainWindow::NetPlayJoin()` / `Config::NETPLAY_HOST_CODE`
   already implement "join via host code" end to end today, just driven by
   the Netplay Setup Dialog's own text field. A new `Host_NetPlayDirectConnect(code)`
   that sets that config value and calls the existing, already-working
   `NetPlayJoin()` would reuse 100% of tested connection logic rather than
   reimplementing `NetPlayClient` construction - much safer than inventing
   new threading code. Just noting this so it doesn't need re-deriving, not
   claiming it's ready to write blind.)
5. **[brawlback-asm #73](https://github.com/Brawlback-Team/brawlback-asm/issues/73)
   "costumes not syncing... P2 loading in as the first secret costume"** -
   **STILL UNRESOLVED. An earlier fix attempt in this session (commit
   `cbfdc87`, changing the host's read from `playerSettings[1]` to `[0]`)
   was wrong and has been reverted (`8035041`) - see the 2026-07-29
   "ProcessGameSettings lead, resolved" section above for the full corrected
   trace.** Short version: the host doesn't receive the client's raw
   `CMD_START_MATCH` payload (where `[0]` really is always the client's own
   data) - it receives the client's own already-merged rebroadcast, where by
   that point `[1]` is the client's real data and `[0]` is the host's own
   data echoed back. So `[1]` was correct the whole time; the "fix" would
   have made both players show the host's costume. Reverted, no net change
   from the pre-session baseline other than removing the bad attempt. The
   original bug report's real root cause is still open - worth checking
   next whether `this->gameSettings` on the host stays intact between its
   own broadcast and the client's reply, and whether the ASM-side
   `MergeGameSettingsIntoGame`/CSP-rendering code's P1/P2 assumptions line up
   with this struct's layout. Needs a live two-client test or Ghidra-level
   tracing, same as #1/#75/#76.
6. **[brawlback-asm #72](https://github.com/Brawlback-Team/brawlback-asm/issues/72)
   direct-connect payload plumbing - now DONE on both ends.** ASM side sent
   the real mode+code payload as of commit `3ee5792` (see write-up above);
   Dolphin side now parses it instead of forcing `UNRANKED` (local commit
   `211f5de` in `/workspace/brawlback-team-dolphin`, on top of `8035041` -
   same unpushed-pending-fork situation). Removed the dead
   `#ifdef REMOVE_THIS_WHEN_PAYLOAD_IS_SET` branch and fixed the
   `SlippiMatchmaking`→`Matchmaking` cast-type typo. Still genuinely blocked
   after this: nothing calls `NetMenu::SubmitDirectConnectCode()` with a
   real player-typed code yet (CSS name-entry offsets still unknown, see
   above) - so DIRECT mode is wired end-to-end but has no UI entry point
   yet. UNRANKED (the only mode currently ever requested) is unaffected
   either way since it doesn't touch the connect-code path.
7. Lower priority / less netcode-central: #76 (game-end/CSS-return workflow),
   #71 (BrawlHeaders repo org migration), #70 (menu game-object reverse
   engineering).

   **#75 ("Pause Workflow... barebones setup") investigated 2026-07-29, not
   implemented - but now root-caused precisely, not just vague.**
   `PlayerFrameData` has two pad fields, `pad` and `sysPad`, clearly meant
   for different purposes (gameplay input vs. some separate "system" input
   path, presumably for things like pause that shouldn't be subject to
   rollback resimulation the way gameplay input is). `getGamePadStatusInjection(status,
   port, isGamePad)` has an `isGamePad` parameter that selects between
   `frameData.pad` and `frameData.sysPad` when injecting received input back
   into the game - **but it's called from exactly one place
   (`ProcessGameSimulationFrame`), always with `isGamePad=true`.** The
   `sysPad`/"system" injection path is 100% dead code - scaffolded (the
   field exists, the parameter exists, the selection logic exists) but never
   wired up to run. Compounding this: `PopulatePlayerFrameData` currently
   populates `sysPad` as an exact duplicate of `pad` (`Util::GamePadToBrawlbackPad(FrameLogic::inputBuffer)`
   for both), so even if something did call the `isGamePad=false` path today,
   it would behave identically to the game path anyway. There's also a
   commented-out `if(ddst->newPressedButtons == 0x1000){ bp(); }` nearby (a
   breakpoint on what looks like a Start-button bitmask check) suggesting
   the original author was actively debugging this exact thing and left it
   mid-attempt.

   This matches "barebones setup" precisely: the scaffolding for a separate,
   rollback-safe pause/system input channel exists, but was never finished
   or connected to anything. **Not implemented because completing it means
   knowing where in Brawl's own code the pause menu actually polls input**
   (to add the missing call site and figure out what `sysPad` should
   actually contain to be meaningfully different from `pad`) - same
   Ghidra/decomp-access blocker as #1/#70/#72's remaining piece, not
   something to guess at in input-injection code where a wrong guess could
   break gameplay input generally, not just pause.

   **#76 investigated 2026-07-28, not implemented** - its own text bundles a
   crash bug with three separate feature asks (report to Lylat, return to
   CSS with player info, live-update opponent's screen on character change).
   Found real, relevant existing code: `Match::PopulateGameReport`'s actual
   stock/damage-reading logic is entirely commented out, and
   `Match::SendGameReport`'s call site is permanently disabled behind
   `#if 0` in `StopGameScMeleeHook` - this is exactly the "report winner to
   Lylat" part of the issue, already half-written but deliberately turned
   off (unclear why - maybe the `Fighter`/`ftManager` API calls it uses were
   never verified safe). Also confirmed `Scene::VsResult` (the results-screen
   scene ID, `0xc`) is defined but never referenced/hooked anywhere in
   `Rollback_Hooks.cpp` - there's no netplay-specific handling at all for
   the transition into or out of the post-match results screen.
   **Didn't attempt a fix**: the actual crash needs either a real repro/crash
   log to diagnose precisely, or the same Ghidra/decomp access blocking
   #1 and #72's text-entry widget, to safely write new scene-transition hook
   code - guessing at this without either risks a worse or different crash
   than the one being fixed. The three feature asks beyond the crash are
   genuinely new subsystems (especially "live-update opponent's screen"),
   not a quick fix.

### 2026-07-29: real, build-verified bug found and fixed - missing endianness swap on GameSettings

While re-investigating #73 (see the correction above), traced `CheckIsMatched()`
in this repo's `Rollback_Hooks.cpp` and found `FixGameSettingsEndianness()`
(defined at the bottom of `namespace Netplay`, swaps `stageID`, `randomSeed`,
and every player's `nametag[]`) was **never called anywhere** - confirmed by
grepping the whole file. Compared this against the two other places raw
structs get read off EXI: `GetInputsForFrame()` reads a `FrameData` and
immediately calls its sibling `Util::FixFrameDataEndianness()`; the
`framesToAdvance` read immediately calls `Utils::swapByteOrder()` directly.
`CheckIsMatched()` was the only one of the three EXI-read sites skipping its
fix step - `FixGameSettingsEndianness` looks purpose-built for exactly this
call site (matching field-for-field) but was simply never wired in, almost
certainly an oversight rather than a deliberate omission.

**Fixed** (brawlback-asm commit `77da4ad`): added the missing
`FixGameSettingsEndianness(gameSettings);` call between the `memmove` and
`MergeGameSettingsIntoGame(gameSettings)` in `CheckIsMatched()`. **Build-verified**
with a full `make` from the repo root (`python3 ./bbk.py setup && make`) -
links and outputs `Brawlback-Online.rel` cleanly, only pre-existing unrelated
warnings.

Concretely, before this fix: `stageID` (a `bu16`) and every player's
`nametag[]` (`bu16[8]`, used for CSS name-tag display) arrived
byte-swapped/wrong on every match, since Dolphin (x86, little-endian) builds
this struct and sends it raw over EXI into the Wii's PowerPC (big-endian)
address space. `randomSeed` is also fixed by this same call but is currently
inert either way - `MergeGameSettingsIntoGame` hardcodes a fixed test seed
rather than reading `settings.randomSeed` (a separate, pre-existing
simplification, not something this fix touches). This does **not** explain
issue #73 - `charColor`/`colorFileIndex` are single bytes with no byte order
to correct - but it's a real, independent, now-fixed bug in the same
neighborhood, and plausibly meaningful for "unranked actually working": a
byte-swapped stage ID could easily resolve to an invalid/wrong stage or
undefined behavior depending on what value it lands on.

### 2026-07-29: wired up real stage selection, replacing hardcoded Battlefield

Following the `stageID` endianness fix, checked whether the merged stage
choice actually reaches the game and found it didn't: `FillInMeleeObj()` had
`g_globalMelee.m_meleeInitData.m_stageKind = Stages::Battle;` with a `//
TODO uncomment and use above line, just testing with battlefield` comment
directly above a commented-out `melee[STAGE_ID_IDX] = stageChoice;` line -
leftover pseudocode from before this function was rewritten to use the real
`g_globalMelee` struct (it references `melee`/unqualified `stageChoice`,
neither of which resolve in this function's actual scope - it was never
actually compilable, just a note-to-self, not functioning-then-broken
code).

Before fixing this, checked the Dolphin clone's `Matchmaking::GetRandomStage()`
(the function that picks the non-host's random stage) for real supporting
evidence rather than guessing whether "real" stage selection is actually
ready: it draws from a deliberately curated `m_allowedStages` list -
Battlefield, Final Destination, Metal Cavern, Wario Land, **Pokemon Stadium
2** - with several other stages explicitly commented out (Yoshi's Island,
Dream Land, Green Hill Zone, Smashville, FoD) as presumably not yet
rollback-safe. Pokemon Stadium 2's presence lines up directly with this
session's separate Stadium-transformation fix (see way above) - concrete,
cross-repo confirmation that this stage-selection system is a real, built
feature waiting on the ASM side, not a half-finished guess.

*(Correction/clarification found later while reading the rest of
`Matchmaking.cpp`'s `handleMatchmaking()`: this hardcoded list is
explicitly labeled in its own source as the **fallback** case - `//
Default case, shouldn't ever really be hit but it's here just in case` -
the real, primary source is `m_allowedStages` populated from the
matchmaking server's response (`getResp["stages"]`), which this session
obviously can't see (that's lylat.gg's live infrastructure). Doesn't
change the conclusion above - the fallback list alone is still real,
deliberate evidence that stage curation is a designed, working concept -
just correcting which one is actually primary vs. backup for accuracy.)*

**Fixed** (brawlback-asm commit `6d70df1`): replaced the hardcoded
`Stages::Battle` with `static_cast<Stages::srStageKind>(GMMelee::stageChoice)`
- `stageChoice` is populated by `PopulateMatchSettings()` from the
network-merged `GameSettings.stageID` before `isMatchChoicesPopulated` is
ever set true, so it's always valid by the time `FillInMeleeObj()` reads it.
Build-verified with a full `make` from the repo root - links cleanly.

**This fix and the endianness fix above are a matched pair**: the
endianness fix makes `stageID` transmit correctly over EXI; this fix makes
the game actually use it instead of discarding it for a hardcoded
Battlefield. Neither one alone would have produced correct stage variety -
worth remembering that connection if either one is ever reverted in
isolation.

### 2026-07-29: dead-code sweep for more "forgot to call it" bugs - one more lead ruled out, rest confirmed genuinely dead

After the endianness and stage fixes above both came from the same pattern
(a function that's clearly purpose-built for one call site but never wired
in), did a systematic sweep of `Rollback_Hooks.cpp` for every function
defined but referenced nowhere else in the file. Found: `SendFrameCounterPointerLoc`,
`ShouldSkipGfTaskProcess`, `StallOneFrame`, `SubmitDirectConnectCode` (already
known, see #72 above), `SyncLog`, `TriggerFastForwardState`,
`fixPadInconsistency`, `getFramesToAdvance`. Checked each rather than
assuming they're all bugs like the stage one was - most are not:

- `getFramesToAdvance`/`TriggerFastForwardState`/`StallOneFrame` are
  convenience wrappers around the `FrameAdvance::framesToAdvance` global -
  but that field is read/written **directly** in over a dozen other places
  in this same file (confirmed via grep), including inline asm blocks. The
  wrappers are simply superseded/never-adopted alternate API, not a missing
  call - the actual mechanism works fine without them.
- `ShouldSkipGfTaskProcess` duplicates logic that's already inlined directly
  in `gfTaskProcessHook()` (the actual registered hook) - same
  `strstr(nonResimTasks, taskName)` check, just written twice. Dead/superseded,
  not missing.
- `SyncLog` is a debug/logging helper (matches the `printInputs`/
  `printFrameData`/`printGameInputs` family nearby) - optional dev tooling,
  fine to leave unused.
- `fixPadInconsistency` - lower confidence, didn't fully trace whether its
  `g_gfPadSystem->updateLow()` call is needed outside of a match; not
  touched.
- `SendFrameCounterPointerLoc` **initially looked like a real gap** - the
  Dolphin clone has a live `handleFrameCounterLoc()` handler
  (`CMD_SEND_FRAMECOUNTERLOC`) that registers the frame counter's address as
  a savestate `staticRegions` entry, so an uncalled sender looked exactly
  like the endianness/stage pattern. But tracing further: its sibling
  senders `CMD_SEND_ALLOCS`/`CMD_SEND_DEALLOCS` (`ProcessGameAllocation`/
  `ProcessGameFree`) are only invoked from `alloc_gfMemoryPool_hook`/
  `free_gfMemoryPool_hook` - both of which are **commented out** in
  `InstallHooks`. So the entire dynamic savestate-memory-region-tracking
  subsystem (allocs, deallocs, and presumably the frame-counter static
  region too) is currently, deliberately inactive as a whole - this isn't
  one forgotten call, it's a disabled subsystem. Did **not** wire this one
  in on its own; re-enabling just the frame-counter piece without
  understanding why the alloc/dealloc tracking is disabled risks being
  wrong in a way that's hard to detect without live savestate testing.
  Flagging for whoever next investigates savestate/rollback memory
  handling specifically.
- Also revisited a "controller port" theory for #73 (was local player's
  CSS slot always index 0, same as `fillOutGameSettings` assumes?) - ruled
  out: `localPlayerPort` is already a known, deliberately-hardcoded-to-0
  simplification on the Dolphin side (documented above, "TODO: assign
  proper stuff based on reality... assume p1 vs p1"), so this is consistent
  with an existing documented limitation, not a new/different bug.

### 2026-07-29: no first-run onboarding flow at all - confirmed real, confirmed not safe to just restore

Checked what a brand-new user (no ISO, no Dolphin downloaded) actually sees
on first launch. `App.tsx`'s routes go straight to `AppBase`/`HomePage`
unconditionally - the `initialized` flag only gates a loading spinner, not
any setup/onboarding state. There's a whole, apparently complete QuickStart
wizard (`containers/QuickStart/`: `IsoSelectionStep`,
`ImportDolphinSettingsStep`, `ActivateOnlineStep`, `LoginStep`,
`SetupCompleteStep`, all driven by `useQuickStart.ts`'s step machine) and
its entry point `views/LandingView.tsx` - but **`LandingView` has zero
importers anywhere in the app**, same dead-code shape as `MainView`/
`SettingsView` found earlier today. It is never shown, to anyone, ever.

**This is not a hard blocker** - `pages/base/SettingsPage.tsx` (the live
Settings, fixed earlier today) has everything a user needs to self-serve:
ISO picker (`MeleeOptions`/`ModsOptions`), Dolphin install/reinstall
(`DolphinSettings`, confirmed has real install functionality). A user who
finds Settings on their own can get fully configured without QuickStart at
all.

**Checked whether restoring `LandingView` would be a safe, scoped fix like
the Settings-tabs one earlier - it would not be.** `useQuickStart.ts`'s
`generateSteps()`/effect both put `QuickStartStep.LOGIN` as the mandatory
*first* step whenever `!options.hasUser` - which, given the login backend
is Slippi's unconfigured Firebase (see the login-flow finding above), is
*always* true. Wiring this wizard back in as-is would trap every new user
on an unpassable login screen before they ever reach ISO selection - strictly
worse than the current state, where Settings is directly reachable with no
gate at all. **Left dead, deliberately, with the reasoning on record**: a
real fix needs either Lylat's actual backend (so LOGIN can actually
succeed) or a product decision to make LOGIN skippable/optional ahead of
ACTIVATE_ONLINE/SET_ISO_PATH - both bigger calls than this session should
make unilaterally. Whoever picks this up next has the full wizard already
built (`containers/QuickStart/*`) and just needs one of those two things to
safely re-route `App.tsx` to it for first-run users.

### 2026-07-29: real launcher bug fixed - stale-mod filter was a silent no-op

Read `settingsManager.ts` in full (main-process settings persistence
layer, only seen in fragments before this session). Found a classic JS
bug in the constructor: a block meant to remove mods whose `elfPath`/
`sdCardPath` no longer exist on disk called `restoredSettings.mods.filter(...)`
but never assigned the result back anywhere.
`Array.prototype.filter()` doesn't mutate in place - it returns a new
array - so this whole check was a silent no-op. Mods pointing at files
that had been deleted/moved/on an unplugged drive would stay in settings
forever, right next to the `isoPath` existence check one block above it,
which *does* correctly reassign (`restoredSettings.settings.isoPath =
null`) - the working sibling made the broken one easy to spot by
contrast.

**Fixed** (launcher commit `8270999`): `restoredSettings.mods =
restoredSettings.mods.filter(...)`. Matches the existing scope/pattern
(in-memory filtering at load time, same as the isoPath check - not
immediately persisted back to the settings file either). Typecheck-clean.

### 2026-07-29: checked addElfPath/addGamePath for an INI key collision - none found, minor fragility noted

Followed `setMod`'s ini-writing (`addElfPath`/`addSdCardPath` in
`dolphin/config/config.ts`, never read before this session) to make sure
selecting a mod doesn't clobber the real Brawl ISO's config -
`addElfPath` writing to `ISOPaths`/`ISOPath1` looked alarming at first
(those read like Dolphin's own ISO-library-folder-scanning keys, nothing
to do with ELF/mod injection). Checked `addGamePath` (used for the real
ISO) alongside it: it uses `ISOPath0`, `addElfPath` uses `ISOPath1` -
different indices, so they don't directly overwrite each other's value.
Dolphin's `ISOPaths`/`ISOPathN` mechanism is its standard multi-folder
game-library scan list; this fork apparently also uses `ISOPath1` for the
mod's ELF folder specifically (plausible given this is a custom fork, not
verified further).

**Minor fragility, not a confirmed bug**: `addElfPath` hardcodes
`ISOPaths = "2"` unconditionally, while `addGamePath` more carefully
preserves any existing count (`numPaths !== "0" ? numPaths : "1"`). This
only produces the correct total if `addGamePath` always runs before
`addElfPath` in practice - true in the normal flow (ISO gets configured
during initial Dolphin setup, mods get selected afterward via the
Settings/Mods tab), so not fixed, just noted as slightly less defensive
than its sibling.

### 2026-07-29: real launcher bug fixed - deleting a mod dropped every mod after it from the UI

While checking the mod-management backend (prompted by the Project+
finding just above), found a real, confirmable bug in
`src/renderer/lib/hooks/useMods.ts`'s `deleteMod`. It correctly calls the
main-process handler first (`window.electron.settings.deleteMod(index)` →
`settingsManager.deleteMod`, which uses `modList.splice(id, 1)` - verified
this removes exactly the one targeted mod, correct), but the renderer's
own optimistic UI state update used `mods.mods.slice(0, index)` - which
keeps everything *before* `index` but drops `index` *and everything after
it*, not just the one deleted mod.

Concretely: with mods `[A, B, C, D]`, deleting `B` (index 1) left the UI
showing just `[A]` instead of `[A, C, D]` - even though the persisted
`settings.json` was correct the whole time (this is a UI-display bug, not
data loss - a full app restart would show the correct list again, since
it re-reads from the correctly-updated settings file). Still a real,
easily-triggered bug for a common action (anyone with more than one mod
installed, deleting anything but the last one in the list).

**Fixed** (launcher commit `b7bf61b`): changed to
`[...mods.mods.slice(0, index), ...mods.mods.slice(index + 1)]`, matching
`splice(index, 1)`'s semantics in an immutable/functional style consistent
with the rest of this zustand store. Typecheck-clean.

### 2026-07-29: launcher's Project+ mod install is unimplemented (self-documented, fails loudly)

Checked `src/mod/installation.ts`/`src/mod/util.ts` (mod download/install
backend for the launcher's "Mods" tab, never examined before this
session). The launcher's `DefaultMods` enum
(`src/settings/types.ts`) has exactly two options: `ProjectPlus` ("P+")
and `vBrawl`. `fetchModLatestVersion()`:

```ts
export const fetchModLatestVersion = async (mod: DefaultMods) => {
  //TODO add api calls
  switch (mod) {
    case DefaultMods.vBrawl:
      return { version: 3.0, downloadUrl: "https://github.com/Brawlback-Team/vBrawlLauncherReleases/releases/download/3.0/Vanilla.V3.Launcher.+.SD.zip" };
    case DefaultMods.ProjectPlus:
      return { version: 3.0 };  // no downloadUrl at all
  }
};
```

`vBrawl` has a real, working Brawlback-Team release URL. **`ProjectPlus`
has no `downloadUrl` at all** - matches the file's own `//TODO add api
calls` comment, so this is self-documented incompleteness, not a hidden
landmine. Confirmed it fails loudly rather than silently or crashing:
`ModInstallation.downloadAndInstall()` in `installation.ts` has
`if (!downloadUrl) { throw new Error(...) }` right where it's needed.

**Worth flagging clearly anyway**: Project+ is a major, popular Brawl mod
- `Project-Plus-Dolphin` was one of the originally-tracked repos in this
whole effort (see the very top of this doc's repo map) - so this isn't a
minor/obscure gap, it's half of the launcher's stated mod support being
entirely unimplemented. Not fixed this session (no Brawlback-Team release
infrastructure for Project+ to point at is visible anywhere in what this
session has access to - same "needs real external info this session
doesn't have" situation as the login backend).

**Sharper picture, found right after**: `defaultSettings.ts` ships "P+"
as a **pre-seeded default mod entry** (`{ name: "P+", ..., default: true
}`, alongside "vBrawl") - and `ModsOptions.tsx` renders zero edit/delete
icons for any mod where `mod.default === true`
(`{!mod.default && <EditIcon .../>}` / `{!mod.default &&
<DeleteIcon .../>}`). So this isn't just "a user manually tries to add
Project+ and hits an error" - **it's a permanently visible, non-removable
entry in the Mods list from the very first launch**, that will always
fail if selected, since there's nowhere for it to actually download from.
Worth knowing precisely how visible this gap is to a real user.

(Also noticed in the same pass: `settings.dolphinPath` (singular, in
`defaultSettings.ts`) is dead - grepped and it's never read anywhere
except its own default declaration. The real, live fields are
`netplayDolphinPath`/`playbackDolphinPath`. Harmless unused cruft, not
touched.)

**Positive finding, worth recording too - vBrawl's mod pipeline is
actually confirmed working end-to-end, not just "looks plausible".**
Since this session has real network access, actually downloaded
`vBrawlLauncherReleases`' `3.0` release zip and inspected its contents
rather than assuming: it contains exactly `Brawl Netplay V3.elf` and
`sd.raw` at the archive root (1.49MB + 537MB, no subfolder nesting).
`ModInstallation._installMod()` extracts straight into
`modsDir/vBrawl/` (`zip.extractAllTo(destinationFolder, true)`), which
produces exactly `modsDir/vBrawl/Brawl Netplay V3.elf` and
`modsDir/vBrawl/sd.raw` - matching `defaultSettings.ts`'s expected
`elfPath`/`sdCardPath` for vBrawl exactly. Real URL, real file, correct
internal structure, correct extraction target - genuinely verified, not
just plausible-looking. Good contrast with Project+: one of the two
default mods is solid and ready, the other has nothing behind it at all.

Also re-confirmed `lylat.gg` itself is live, real infrastructure right
now (`curl -IL https://lylat.gg` → `200 OK`, real nginx/Ubuntu headers) -
consistent with earlier sessions' findings, just re-verified fresh. Can't
test the actual ENet/UDP matchmaking protocol (port 43113) from here
without a full Dolphin build and real client handshake, but the domain
being live and serving is good corroborating evidence the backend
Brawlback points at is operational, not dead/parked.

### 2026-07-29: macOS support was fully broken - wrong app bundle name everywhere, now fixed

While reading `install/installation.ts` in full, `userFolder`/`sysFolder`
both hardcoded macOS paths through `"Slippi Dolphin.app"`. Checked whether
that's actually right before assuming a bug - it isn't. The Dolphin
clone's own build config is authoritative here: `DolphinQt/CMakeLists.txt`
(APPLE block) has `set(BUNDLE_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/DolphinQt.app)`
and `set_target_properties(dolphin-emu PROPERTIES ... OUTPUT_NAME DolphinQt)`
- the real bundle is **`DolphinQt.app`**, containing a binary named
**`DolphinQt`** (at `Contents/MacOS/DolphinQt`), not "Slippi Dolphin"
anywhere.

Grepped for every other occurrence and this turned out to be a
much bigger, more thorough sweep than expected - **six more instances**
across three more files, all wrong the same way:

- `dolphin/util.ts`'s `findDolphinExecutable`: the darwin filename check
  (`endsWith("Dolphin.app")` - would have coincidentally matched
  "DolphinQt.app" too, red herring) and, critically, the binary path
  construction (`Contents/MacOS/Slippi Dolphin`) - this one doesn't
  coincidentally work, it's the actual reason launching Dolphin on macOS
  would fail even if the `.app` were found.
- `dolphin/install/macos.ts` - the actual DMG-install pipeline: resource
  path detection, the extracted-files cleanup filter (`file !== "Slippi
  Dolphin.app"` - would have deleted the real, correctly-named
  `DolphinQt.app` right after extracting it, along with everything else!),
  permission-fixing (chmod/chown), and the update-in-place User-folder
  migration all referenced the wrong names.
- `dolphin/ipc.ts` - a doc comment with the same wrong name (cosmetic,
  fixed for accuracy, no functional effect).

**Fixed** (launcher commit `23a5540`), all four files, typecheck-clean.
This is a significant one: **macOS support was fully broken, not just
degraded** - between the wrong binary path (can't launch Dolphin at all)
and the cleanup-filter bug (would have deleted the real app right after
installing it), no macOS user could have gotten this launcher working at
all before this fix, regardless of anything else in this session's
findings. Confirmed via the Dolphin fork's own authoritative build
config, not guessed - same standard of evidence as the earlier
`playkey.ts` path fix.

**CORRECTION + much bigger follow-up, same session, commit `fb26d04` -
read this before trusting `23a5540`'s bundle name.** After the fix
above, decided to verify against the actual live release bytes rather
than stop at the CMakeLists.txt (which only proves what a *fresh build
from current source* would produce, not necessarily what's *actually
distributed* - a real distinction, and this time it mattered). Downloaded
`project-lylat/dolphin`'s real "latest" release assets for all three
platforms and inspected them directly:

- **The real macOS bundle is `Dolphin.app` / `Dolphin`, not
  `DolphinQt.app` / `DolphinQt`.** The distributed 5.0 release (dated
  2022-06-07) predates whatever renamed the CMake `OUTPUT_NAME` to
  `DolphinQt` - `23a5540`'s fix was well-evidenced but referenced a name
  that isn't actually what's downloadable today. Corrected all four
  files back to `Dolphin.app`/`Dolphin`.
- **Far more serious**: `installDolphinOnMac` called `extractDmg()`,
  which requires a real `.dmg` and throws immediately otherwise
  (`Expected a dmg file, got ...`). The actual downloaded asset is a
  `.zip` (`LylatDolphin-macos.zip`) - extraction failed on literally
  every install attempt, before the bundle-name question was ever even
  reached. Worse: that zip contains a single nested file misleadingly
  named `*.tar.gz` that's actually zip-formatted content (confirmed with
  `file` and by successfully opening it with `adm-zip`), which itself
  contains the real `Dolphin.app`. Rewrote `installDolphinOnMac` to do
  two-stage `adm-zip` extraction, dropping the `dmg`/`extractDmg`
  dependency for this path entirely.
- **Linux had the identical nested-archive shape**: the downloaded zip
  contains one nested zip (versioned filename) containing the real
  `Lylat_Online-x86_64.AppImage` + its `.zsync` file.
  `installDolphinOnLinux` only extracted once, leaving the AppImage
  buried inside an unextracted inner zip. Added the second extraction
  stage. Also fixed `findDolphinExecutable`'s Linux detection, which
  checked for `"Slippi_Online"`/`"Slippi_Playback"` filename prefixes -
  the real file is `Lylat_Online-x86_64.AppImage` (matches neither, and
  Lylat ships one unified build for both launch types anyway, so the
  Online/Playback distinction doesn't even apply) - changed to a robust
  `filename.endsWith(".AppImage")` check.
- **Windows had a different but related issue**: the downloaded zip
  wraps every file under one top-level folder
  (`LylatDolphin-Windows/Dolphin.exe`, not `Dolphin.exe` at the root),
  which `findDolphinExecutable`'s single-level directory scan would never
  find. Added a flattening step to `installDolphinOnWindows` that moves
  the wrapper folder's contents up one level after extraction.

**All three platforms' fixes were functionally tested against the real,
live release files** - downloaded via `curl`, extracted with the exact
same `adm-zip`-based logic now in the source, verified the resulting
directory structure matches what `findDolphinExecutable` expects
(wrote a standalone Node script exercising the actual extraction logic,
not just read the code and reasoned about it). This is the strongest
verification standard used anywhere in this session, short of running
the real Electron app - confirmed:
`Dolphin.app/Contents/MacOS/Dolphin` exists after macOS extraction,
`Lylat_Online-x86_64.AppImage` ends up findable at the Linux extraction
root, `Dolphin.exe` ends up flattened to the Windows extraction root.

**Net effect of `fb26d04`: Dolphin installation was completely
non-functional on all three platforms before this fix** - not degraded,
completely broken, on every OS the launcher supports. macOS threw on the
very first extraction call. Linux silently produced an unusable nested
zip sitting where the AppImage should be. Windows extracted successfully
but the executable could never be found, one folder level too deep. This
is very plausibly the single highest-impact fix in the entire session -
without it, literally nobody on any platform could get past Dolphin
installation at all.

### 2026-07-29: Help menu sent users to Slippi's real Discord server, not Brawlback's

Checked `dolphin.service.ts`/`useDolphinListeners.ts`/`handleDolphinExitCode.ts`
per the standing plan from last checkpoint - the first two were clean,
but `handleDolphinExitCode.ts` had leftover `"the Slippi Discord"` text in
its Windows/Linux crash-message strings (same pattern as the earlier
`SupportBox.tsx` fix, commit `7968663`, just missed since it lives in a
different directory). Swept for more and found something more serious:
`main/menu.ts`'s Help menu (`"Open Slippi Discord Server"`, present in
*both* the macOS and Windows/Linux menu template variants) hardcoded
`http://discord.gg/pPfEaW5` - **Slippi's real, actual Discord invite
link**. Clicking Help → Discord from this launcher sent users to the
wrong community's server entirely, not a broken/cosmetic link but an
actively-wrong destination.

**Fixed** (launcher commit `974bc62`): both menu items now use the
already-correct `socials.discordUrl` constant (`common/constants.ts`,
fixed earlier this session in `b0da467`) instead of a second, independent
hardcoded copy of the wrong URL, and are relabeled "Open Brawlback
Discord Server". Also fixed the three "Slippi Discord" text leftovers
(`dolphin/util.ts`'s "no Dolphin found" error,
`handleDolphinExitCode.ts`'s two crash-report messages). Typecheck-clean.

Worth noting for future sweeps: this confirms hardcoded-URL duplicates
(not just hardcoded text) can hide in unexpected files (`main/menu.ts`
isn't somewhere the earlier "fix all Slippi text" pass would obviously
look) - a `grep -r` for the literal old URL/text across the *whole* `src/`
tree, not just the renderer, is worth repeating periodically as more of
this codebase gets read.

### 2026-07-29: real, high-visibility bug fixed - Project+ auto-download errored on every single launch

Read `useAppInitialization()` (`renderer/lib/hooks/useApp.ts`, the
function that runs unconditionally on every app start, before this
session never checked). Found it auto-downloads *both* default mods on
every launch:

```ts
[DefaultMods.vBrawl, DefaultMods.ProjectPlus].map(async (m) => {
  return dolphinService.downloadDefaultMod(m).catch((err) => {
    showError(`Failed to install ${m} Mod. Error: ...`);
  });
});
```

Given the already-documented Project+ finding (`fetchModLatestVersion`
has no `downloadUrl` for `ProjectPlus`, always throws), and confirmed by
re-reading `ModInstallation.validate()` closely: a failed install never
reaches `_installMod()`, so `metadata.json` never gets written - meaning
there's no "already tried and failed, don't retry" state to fall back on
between launches. **This meant every single user saw a "Failed to
install ProjectPlus Mod" error toast on every single app launch,
unconditionally** - not just when opening the Mods tab, not just once,
forever, for every user, regardless of whether they ever cared about
Project+ at all. Much more disruptive than the earlier "permanently
broken menu entry" framing suggested - this is the app's very first
impression on every single launch.

**Fixed** (launcher commit `52a4fc7`): removed `DefaultMods.ProjectPlus`
from this automatic startup list. Zero functional cost - Project+ never
successfully installs either way, and (per the earlier finding) there's
no UI path to manually trigger a reinstall anyway, since default mods get
no edit/delete affordance in `ModsOptions.tsx`. "Never attempted" and
"always fails loudly" are functionally identical outcomes for the
feature itself; this just removes the recurring, unconditional error
noise. Re-add once Brawlback-Team hosts a real Project+ release.
Typecheck-clean.

### 2026-07-29: dead Melee-specific Gecko code found (geckoCode.ts/config.ts) - inert, flagged not fixed

While checking `IniFile`'s only real consumers (`geckoCode.ts`,
`config.ts`'s `setBootToCss`), found `updateBootToCssCode`
(`dolphin/util.ts`) hardcodes a "Boot to CSS" Gecko code -
`041BFA20 38600002`, credited to "Dan Salvato, Achilles" (well-known
Melee modding community figures) - a **Melee-specific memory address**
(GALE01's address space), not Brawl's (RSBE01, entirely different
memory layout). Applying this to a Brawl instance would be meaningless
at best.

**Confirmed inert, not touched**: `updateBootToCssCode` has zero callers
anywhere in the codebase, and "Boot to CSS" isn't referenced from any UI
component either - there's no way for a user to currently trigger this
at all. Consistent with the session's broader pattern (Slippi-inherited
leftovers that are either live-and-need-fixing, like the install
pipeline, or dead-and-harmless as long as they stay disconnected, like
this). Flagging clearly rather than "fixing" dead code: if a future
"boot directly to CSS" feature ever gets wired up for Brawl, it needs a
real Brawl-specific Gecko code at the right address, not this Melee one
carried over unchanged.

### 2026-07-29: real bug fixed in IniFile.ts's save() - could drop Gecko-code lines

User asked to skip anything needing a live test and keep auditing code.
Checked `dolphin/config/iniFile.ts` (the shared read/write layer under
every single Dolphin config operation - `addGamePath`, `addElfPath`,
`addSdCardPath`, `setSlippiSettings`, everything fixed earlier today
goes through this), never verified directly before.

Found a real bug in `save()`: a `Section` can legitimately hold both raw
verbatim lines (comments, or Gecko-code-style lines starting with
`$`/`+`/`*` - the *loading* code explicitly supports mixing these with
regular key=value pairs within the same section) and key=value entries
at the same time. `save()` only ever wrote one or the other -
`if (section.keysOrder.length === 0) { write section.lines } else {
write section.keysOrder }` - so any section with *both* would silently
lose its raw lines (Gecko codes, most likely) on every save.

**Fixed** (launcher commit `84211af`): write `section.lines` first, then
`section.keysOrder`, in the non-empty branch too - matches what loading
already supports, so `save()` now round-trips everything `init()` can
produce. Verified the exact fixed logic directly (a section with one raw
Gecko-style line and one key=value pair - both now survive) since
`ts-node`'s path-alias resolution didn't cooperate cleanly outside the
full webpack build for a true import-based test.

Doesn't appear to be actively biting anything else this session touched
- the sections written by `addGamePath`/`addElfPath`/etc. (`General`) are
plain key=value only in practice - but this is foundational, shared
infrastructure, worth getting right regardless of whether today's other
fixes happen to dodge it.

### 2026-07-29: verified --version/_isOutOfDate for real, fixed a small download.ts inconsistency

Two follow-ups after the big install-pipeline fix:

- **Verified `_isOutOfDate`'s `--version` dependency is real, not
  assumed.** Checked the Dolphin clone's actual CLI parsing
  (`UICommon/CommandLineParse.cpp`: `parser->usage(...).version(Common::GetScmRevStr())`,
  using the `optparse` library which auto-registers a working
  `--version` flag from a `.version()` call) and the real output format
  (`Common/Version.cpp`: `"Dolphin " ["[branch] "] + SCM_DESC_STR`, e.g.
  `"Dolphin [master] 5.0-19870-g1234567"`). Tested `semver.coerce()`
  against that exact noisy format (not just the bare `"5.0"` tested
  earlier) - correctly extracts `5.0.0`. This confirms the earlier
  `_isOutOfDate` fix (from much earlier this session) genuinely works
  against what Dolphin actually outputs, not just a plausible guess.
- **Fixed a small, real inconsistency in `utils/download.ts`** (launcher
  commit `d1cca92`): `createWriteStream` used `{ flags: "wx" }`
  (exclusive create - fails if the destination already exists)
  unconditionally, contradicting the function's own `overwrite`
  parameter, which is supposed to permit exactly that. Changed to `"w"`.
  Low real-world severity today since both real callers already
  pre-check `fileExists` before calling `download()` at all, so this
  specific path isn't currently reachable in normal use - but a genuine
  latent inconsistency, cheap and safe to close while already in this
  file's neighborhood.

### 2026-07-29: rollback resimulation orchestration - traced end to end, one suspected bug ruled out, one narrow edge case noted

Traced `handleFrameDataRequest` → `getRemoteInputs` → `getLocalInputs` /
`updateSync` (the actual per-frame "what inputs does the game get this
frame" orchestration) and `ProcessRemoteFrameData` →
`ProcessIndividualRemoteFrameData` (the receive-side queueing) end to end
- this is as deep into the live resimulation trigger path as this session
got.

- **Initially suspected dead code**: `handleFrameDataRequest`'s per-player
  loop has `if (this->framesToAdvance != 0) { for (...) { if
  (this->framesToAdvance == 0) { ...use blank data... } ... } }` - the
  inner check looked unreachable given the outer guard already ensures
  `!= 0`, and nothing in between appeared to change it. **Checked
  `getRemoteInputs` before flagging this and found it's not dead**: in
  delay-based (non-rollback) mode, when no remote frame data is found,
  `getRemoteInputs` sets `this->framesToAdvance = 0` as a side effect
  (line ~437) - meaning an *earlier* player's iteration in the same loop
  can legitimately flip this flag, and the inner check correctly catches
  it for *later* players in that same call. Real, working mechanism, not
  dead code - glad I traced the side effect before reporting this as a
  bug.
- **Confirmed the rollback-vs-delay split is intentional and correct**:
  in delay-based mode, missing remote data stalls (`framesToAdvance = 0`);
  in rollback mode, missing remote data predicts instead (reuses the last
  confirmed input, sets `isPredicting = true`) rather than stalling - this
  is exactly the expected difference between the two netcode strategies,
  not a bug.
- **One narrow, low-confidence edge case, not treated as confirmed**: in
  rollback mode, if there's *no* current remote data *and* no previous
  input to predict from either (`getRemoteInputs`'s innermost fallback,
  line ~410-414), it silently uses blank input and only logs an
  `ERROR_LOG_FMT` - it does not stall. This should only be reachable very
  early in a match, before any remote frame has ever been received at all
  - narrow enough, and dependent enough on match-start sequencing this
  session hasn't fully traced, that I'm not confident enough to call it a
  bug outright. Noting it in case whoever eventually does a live two-client
  test sees a first-frame glitch that lines up with this.

### 2026-07-29: Netplay.cpp / frame-broadcast / ack logic - checked thoroughly, all clean

Read `Netplay.cpp` (Dolphin clone) fully for the first time, and traced
the live per-frame input broadcast path in `EXIBrawlback.cpp` and
`TimeSync::ProcessFrameAck` carefully, since this is the actual per-frame
send/ack mechanism - core to whether netcode holds together at all.

- `BroadcastPlayerFrameData` (singular) is dead, superseded by
  `BroadcastPlayerFrameDataWithPastFrames` - same "earlier version left in
  place" pattern seen elsewhere today, not a bug.
- **Initially suspected an off-by-one in the frame-broadcast loop**
  (`endIdx = localPadQueueSize-1 - (back()->frame - minAckFrame)`, loop
  condition `i > endIdx`) - looked like it might exclude `minAckFrame`
  itself from the resend batch. Traced `TimeSync::getMinAckFrame()` to
  check the exact semantics before concluding anything: it returns the
  *minimum* of `lastFrameAcked[i]` across all remote players - i.e. the
  frame that's already been confirmed received by every peer. Excluding
  it from the resend batch is therefore **correct**, not a bug - it's
  already been acknowledged, so there's nothing to gain by resending it.
  Glad I checked the semantics before flagging this.
- `ProcessFrameAck` (RTT/ping tracking, ack-timer bookkeeping) read
  cleanly - no issues found.
- `TimeSync::shouldStallFrame` (frame-diff stall logic, time-sync skip
  logic) is explicitly a faithful port of Slippi's own proven
  implementation (the file's own header comment says so) - didn't attempt
  to re-derive this algorithm from scratch given the much lower prior
  probability of a fresh bug in ported, previously-working code, versus
  the genuinely novel Brawlback-specific glue code where today's real bugs
  were actually found.

### 2026-07-29: BrawlbackUtility.cpp audit - two dead-code landmines, one "looked huge, actually inert" finding

Read `BrawlbackUtility.cpp` (Dolphin clone) fully for the first time this
session - referenced constantly from `EXIBrawlback.cpp` but never read
end to end before.

- **`isButtonPressed(u16 buttonBits, PADButtonBits button)` ignores its own
  `button` parameter** - the body hardcodes `buttonBits & (PADButtonBits::Z << 8)`
  regardless of what button was asked about. **Confirmed dead code** (zero
  other references anywhere in `Core/`) - currently harmless, but a real
  landmine for whoever eventually wires it up expecting it to check the
  button they pass in. Not fixed (no active caller to fix it for), just
  flagged so it doesn't surprise someone later.
- **`Match::isPlayerFrameDataEqual`** only compares `.pad` fields, never
  `.sysPad` - also **confirmed dead code** (zero other references), same
  "landmine, not active" situation.
- **The one that looked like a big deal and mostly wasn't**: the *live*
  desync-detection check in `EXIBrawlback.cpp`'s `updateSync()` -
  `isInputsEqual((*remoteInputs).pad, playerPredictedInputs.pad)` - has the
  exact same gap (only compares `.pad`, never `.sysPad`), but this one is
  actually *called*, driving the real rollback-trigger decision. Initially
  looked like a serious live bug, especially once `getGamePadStatusInjection`
  in brawlback-asm showed `BrawlbackPad& pad = isGamePad ? frameData.pad :
  frameData.sysPad;` - implying `sysPad` is a real, distinct second input
  path (looked like it might be Wii Remote support, alongside GameCube
  controller support via `pad`). **Traced further and this is currently a
  no-op, not a bug**: `PopulatePlayerFrameData` (`Rollback_Hooks.cpp`)
  populates *both* fields identically from the same source -
  `pfd.pad = Util::GamePadToBrawlbackPad(FrameLogic::inputBuffer);` and
  `pfd.sysPad = Util::GamePadToBrawlbackPad(FrameLogic::inputBuffer);` back
  to back, same input buffer. Since `sysPad` always equals `pad` at the
  point of capture right now, a `.pad` mismatch always implies a `.sysPad`
  mismatch too and vice versa - checking `.sysPad` separately can't
  currently change the rollback decision either way. **Not fixed** -
  wiring in a redundant-right-now check would be pure speculative
  future-proofing for a Wii Remote / alternate-input-device path that
  isn't actually implemented yet (the `isGamePad` branch exists on the
  *injection* side but the *capture* side doesn't yet feed it genuinely
  different data). Worth remembering this connection if that support is
  ever built out: the injection code is ready for it, the capture code and
  this desync check are not.

### 2026-07-29: checked whether BrawlHeaders alone could crack #73 - it can't, confirms existing blocker

Tried one more angle before calling #73 fully blocked: `BrawlHeaders` is a
real, usable project (not a guess), so maybe the actual costume-file
loading structures were declared there without needing Ghidra. Checked
`gm_sel_char_data.h` (confirms `gmSelCharData::m_playersInitData` is the
same `gmPlayerInitData` struct/layout used by `g_globalMelee`, so no
struct-mismatch between the CSS-selection-time data and match-init-time
data - consistent with what was already ruled out), `it_gen_archive.h`
(turned out to be item/pickup generation - Assist Trophies, Pokeballs,
crates - "it" is "item," not costume-related at all, a wrong guess), and
`mu_selchar_player_area.h` (has an `m_charColorNo` field, but that's a
CSS-portrait-rendering-time struct, not the match-init consumer).

**This doesn't get further than the existing blocker, and confirms why**:
`BrawlHeaders` only declares *where data lives in memory* (struct
layouts) - it says nothing about what the game's actual compiled code
*does* with `m_colorNo`/`m_colorFileNo` once FillInMeleeObj writes them.
That logic only exists in the game's binary. #73 stays exactly where it
was - genuinely needs Ghidra/decomp access or a live repro, not more
header-reading.

### 2026-07-29: Dolphin's Brawlback settings pane - two of three setting groups are non-functional

Checked `DolphinQt/Settings/BrawlbackPane.cpp` (never examined before this
session) against what the actual netcode reads, since this is the exact
"UI vs. backend" mismatch pattern that's paid off repeatedly today
(playkey path, ModsOptions text, dead launcher tabs). This pane has three
`QGroupBox`s - only one of them does anything.

- **"Delay Frames" spinbox (range 1-7) is completely non-functional.**
  It reads/writes `SConfig::m_delayFrames`, but grepped all of `Core/` and
  found **zero** other references to that field anywhere. The actual input
  delay used throughout the real netcode - `TimeSync.cpp`'s
  `frameDiffCheck`/display string, and `EXIBrawlback.cpp`'s
  `localPlayerFramedata->frame += FRAME_DELAY` - all use `FRAME_DELAY`, a
  **compile-time `const bs32 FRAME_DELAY = 1`** in the shared
  `brawlback-common/BrawlbackConstants.h` submodule (same one brawlback-asm
  uses, so ASM and Dolphin trivially agree on the value by construction -
  the constant even has a `static_assert` tying it to `MAX_ROLLBACK_FRAMES`
  for correctness). Moving this spinbox does nothing at all; the actual
  delay is always 1 frame regardless of what a user sets here.
- **"Save Brawlback Replays" checkbox and "Replay Location" folder picker
  are also non-functional** - `m_brawlbackSaveReplays`/`m_brawlbackReplayDir`
  have zero other references anywhere in `Core/` either. Consistent with
  (not a new surprise on top of) the already-documented fact that
  Brawlback hasn't defined a replay format yet (FAQ.md, and the launcher's
  dead `ReplayBrowserPage`/`MainView` finding earlier this session) - the
  settings UI got built ahead of the feature existing, same pattern as the
  launcher's dead QuickStart wizard.
- **Only "Force Custom Netplay Port"/custom port and "Force LAN IP"/LAN IP
  actually work** - confirmed these `SConfig` fields (`m_slippiForceNetplayPort`,
  `m_slippiNetplayPort`, `m_slippiForceLanIp`, `m_slippiLanIp`) are genuinely
  read in `Matchmaking.cpp`'s `startMatchmaking()`.

**Not fixed, and not a quick fix**: unlike the launcher bugs, there's no
existing, working piece of code to just wire this up to - making "Delay
Frames" real would mean either (a) making `FRAME_DELAY` itself
configurable, which touches a shared compile-time constant with a
determinism-critical `static_assert` relationship to `MAX_ROLLBACK_FRAMES`
that both ASM and Dolphin depend on agreeing on, or (b) adding a new EXI
message so a Dolphin-side runtime setting could be communicated to the
ASM/game side and used instead of the constant - genuinely new protocol
work, not a bug fix. Recording this clearly since a user finding this
spinbox and concluding they've tuned their delay frames would be
completely wrong, and that's worth knowing about even without a fix ready.

### 2026-07-29: re-audited the small brawlback-asm files - nothing new, a few notes worth recording

Went back through the six smaller source files (`mem_exp_hooks.cpp`,
`EXI_hooks.cpp`, `exi_packet.cpp`, `rel.cpp`, `utils.cpp`,
`BrawlbackHeadersImpl.cpp`) with the same careful, line-by-line approach
that found the endianness/stage/IncrementalRB bugs, rather than trusting
an earlier session's "checked once, all boilerplate" note at face value.
Nothing at that severity turned up, but three things worth recording:

- **`BrawlbackHeadersImpl.cpp` is entirely dead code, confirmed.** The
  whole file is gated behind `#if __cplusplus == 199711L` (C++98 only) -
  the real, actively-compiled implementations are the `#else` branches
  with default member initializers, right in the shared headers
  (`GameSettings.h`/`PlayerSettings.h`/etc.) themselves. Confirmed the
  toolchain doesn't compile in C++98 mode: no `-std=` flag anywhere in
  `Brawlback-Online/Makefile`, so clang uses its modern default. This file
  compiles to an empty translation unit - worth knowing so no one mistakes
  it for live code later.
- **`EXIHooks::readEXI` calls `DCFlushRange` (not `DCInvalidateRange`) on
  the DMA destination buffer after every EXI read** - initially looked
  like a cache-coherency bug (using the wrong direction of cache op could
  mean reading stale cached data instead of freshly-DMA'd bytes, which
  would explain all sorts of "random" data corruption). **Did not confirm
  this as a real bug** - real Wii SDK `DCFlushRange` typically implements
  via `dcbf` (flush *then* invalidate), and since DMA writes bypass the
  CPU cache entirely, any stale cached line here would be clean (not
  dirty), so the writeback step is a no-op and the invalidate step alone
  would still correctly evict it - meaning `DCFlushRange` likely achieves
  the same practical effect as `DCInvalidateRange` would here. Can't verify
  the exact instruction-level behavior without disassembly access, so
  this is recorded as **checked, likely fine, not touched** rather than
  either fixed or confidently flagged as broken - didn't want to repeat
  the #73 overconfidence mistake in the other direction (crying wolf on
  something that's probably fine).
- Minor, not worth fixing: `EXIPacket`'s no-arg and 1-arg constructors log
  `this->size` (uninitialized at that point in construction) instead of
  `new_size` in their allocation-failure `OSReport` call - only affects a
  debug log message in a rare OOM path, not actual behavior.

### 2026-07-29: actually dug into IncrementalRB's internals - three real fixes applied

Pushed past the earlier "too risky to touch" caution and actually read
`incremental_rb.cpp`/`mem.cpp`/`tiny_arena.cpp` line by line, since this is
the literal core of whether rollback plays correctly - avoiding it
entirely wasn't good enough. Found three things, fixed two, and got a
solid trace on a third that's too significant to guess-fix blind.

**Fixed #1 (dolphin commit `a2ee4c4`): `IncrementalRB::Shutdown()` was
never called anywhere.** It exists specifically to free each savestate's
`_mm_malloc`'d arena backing buffer and tear down the job system, and its
sibling `IncrementalRB::InitState()` (called from `MemoryManager::Init()`)
unconditionally allocates fresh buffers every time without freeing any
previous ones. Added the call to `MemoryManager::Shutdown()`, the natural
pairing point right where `Init()` set things up. Verified safe even if
ever called without a prior Init (`arena.backing_mem` default-initializes
to `nullptr`, `_mm_free(nullptr)` is a no-op; `jobsystem::ShutDown()` just
joins an empty thread vector if `Initialize()` never ran).

**Fixed #2 (dolphin commit `2f440c4`): off-by-one page overrun in
`GetWrittenPages`.** `Common::GetPageAddress()` rounds down; the scan's
`end_pte` was computed from `base + baseSize` directly. When `baseSize` is
an exact multiple of the page size - the normal case here, since this now
tracks whole physical memory regions via `GetPhysicalRegions()`, which are
page-aligned in both start and size - an already-aligned address is
returned unchanged, landing one page past the buffer's actual last valid
page. The inclusive `base_pte <= end_pte` loop then ran
`IsPageDirty`/`HandleChangeProtection`/`SetPageDirtyBit` on a page outside
the tracked buffer - whatever memory happens to sit immediately after it.
Fixed by computing `end_pte` from the buffer's last valid byte
(`base + baseSize - 1`) instead, which resolves correctly whether or not
`baseSize` is page-aligned.

**Flagged, not fixed - and this one matters**: traced how `OnPagesWritten()`
interacts with `EvictSavestate()`/`arena_alloc()` across repeated
resimulation of the *same* savestate slot, and found a real leak with a
plausible path to an actual crash. `SaveWrittenPages(frame, resim)` only
calls `EvictSavestate()` (which clears `afterCopies` and resets the
arena's bump offset) when `savestate.valid && !resim` - eviction is
skipped whenever `resim` is true. But `OnPagesWritten()`'s alloc loop does
`savestate.afterCopies.push_back(arena_alloc(...))` unconditionally, once
per entry in `savestate.changedPages`, on *every* call - it never accounts
for `afterCopies` already holding entries from a previous call on the same
Savestate object. Traced the indexing carefully: `RollbackSavestate()`
only ever reads `afterCopies[index]` for `index` derived from a position
within the *current* `changedPages` (so the specific data actually
restored during a rollback isn't corrupted by this, as far as I can trace)
- but each repeated resim-without-eviction call still burns fresh
`arena_alloc` calls for entries that get appended and then orphaned,
permanently advancing the arena's bump-pointer offset for space that's
never reclaimed until the next successful non-resim eviction. `arena_alloc`
returns `nullptr` on exhaustion, and neither `OnPagesWritten` nor
`RollbackSavestate` ever null-checks before `memcpy`-ing through it.

Confirmed the scale is plausible, not just theoretical: `MAX_SAVESTATES =
MAX_ROLLBACK_FRAMES + 2 = 7` (`brawlback-common/BrawlbackConstants.h`),
each with a `MAX_NUM_CHANGED_PAGES * PageSize()` ≈ 60000 × 4KB ≈ 234MB
arena. That's generous headroom for any *one* capture, but if the same
savestate slot gets resimulated repeatedly across multiple rollback events
before a normal (non-resim) capture ever evicts it again - plausible over
a long match with a connection that triggers frequent rollbacks - the
orphaned allocations could accumulate enough to actually exhaust that
budget, at which point the next `arena_alloc` returns `nullptr` and the
next `memcpy` through it crashes. This lines up well with "the game
crashes" style reports (#76) being intermittent and correlated with
match length/connection quality rather than reproducible on demand.

**Update - found a fix that doesn't require touching the resim-eviction
question at all (dolphin commit `0a3c0d2`).** Rather than changing *when*
eviction happens (which I still don't have confident insight into - there
could be a real reason resim skips it, e.g. wanting the pre-resim baseline
available for something else within the same pass), the actual waste is
narrower than that: `OnPagesWritten`'s alloc loop can simply start from
`savestate.afterCopies.size()` instead of `0`, allocating only the delta
needed to bring `afterCopies` up to `changedPages.size()` rather than
appending `changedPages.size()` *more* entries regardless of what's
already there. This reuses already-allocated slots in place (they get
fresh data from that call's copy loop either way) instead of abandoning
them. Verified this is a strict no-op for the normal post-eviction case
(`afterCopies` starts empty, so the loop is identical to before) and only
changes behavior in the accumulating-resim case, where `afterCopies.size()
== changedPages.size()` now holds after every call instead of growing
unbounded. Grepped every read site (`RollbackSavestate`'s three index
paths) to confirm none of them ever needed `afterCopies` to be larger than
`changedPages` - they only index by position within `changedPages`, so
this doesn't remove anything any reader was relying on. This is a
genuinely minimal, conservative fix - it doesn't touch or need to
understand *why* eviction is skipped during resim, it just stops the
resulting orphaned allocations from ever happening in the first place.

### 2026-07-29: #73 - one more theory checked and weakened, static-analysis avenues now exhausted

Took a fresh angle: is there a cross-thread race on the merged costume data?
`NetMenu::StartMatching` (which drives `CheckIsMatched()` →
`MergeGameSettingsIntoGame()` → `GMMelee::PopulateMatchSettings()`, the
function that actually writes `costumeChoices[]`/`isMatchChoicesPopulated`)
runs on a genuinely separate `OSThread` (`OSCreateThread(&thread,
Netplay::StartMatching, ...)`), while the main game thread polls
`Netplay::foundMatch` from several different hooked functions
(`BBisCompleteMeleeSettingAllMember` etc., each passing it straight into the
game's own original "is setup complete" polling functions) before
eventually reaching `BootToScMelee()` → `postSetupMelee` →
`FillInMeleeObj()`, which reads `costumeChoices[]`. None of these globals
are `volatile` or otherwise synchronized, which on a true SMP system would
be a real, classic race condition (write visible to thread A "eventually"
but not guaranteed by the time thread B reads it).

**But this theory is significantly weakened by one fact**: the Wii's
Broadway CPU is single-core - `OSThread` is a cooperative/priority-based
scheduler on one physical core, not true SMP, so the cross-core
cache/reordering hazards that would make this a real bug on a multi-core
system mostly don't apply here. Combined with the main thread busy-polling
`foundMatch` across many game-loop frames before ever proceeding (a wide
timing margin in wall-clock terms), this doesn't look like a promising lead
- noting it as **checked and considered unlikely**, not confirmed either
way, since I can't fully rule out an obscure single-core reordering edge
case without deeper RVL/OSThread-internals knowledge this session doesn't
have.

**Where this leaves #73**: every static-analysis-reachable theory this
session could evaluate has now been checked - struct layout (identical,
ruled out), merge-index (ruled out, the original code was already right),
controller-port assumption (ruled out, consistent with the known
`localPlayerPort=0` limitation), and now cross-thread timing (weakened by
the single-core CPU). None of them explain "P2 loading in as the first
secret costume." The remaining candidates all point the same direction as
issue #1's blocker: something in the actual costume/CSP-rendering
consumption code (downstream of `m_colorNo`/`m_colorFileNo` -
`FillInMeleeObj`'s writes into `g_globalMelee`, and whatever Brawl's own
CSS/character-loading code does with those fields) that this session can't
safely reverse-engineer without Ghidra/decomp access or a live two-machine
repro to actually observe what value P2 ends up with. Not re-attempting
further guesses here without one of those - the highest-value next step
for #73 specifically is a live test, not more static reading.

### 2026-07-29: core rollback/savestate audit - one dead class identified (not a bug), rest too risky to touch blind

Went looking for the same class of bug as the endianness/stage fixes
(clear call-wiring mistakes) in the actual rollback/savestate machinery,
since that's the last major unaudited piece of "does a match play
correctly once it starts."

**`BrawlbackSavestate` (Savestate.cpp/.h, Dolphin clone) is entirely dead
code - confirmed, not a bug.** Grepped the whole codebase: nothing outside
its own definition file references the class at all - not instantiated,
`Capture()`/`Load()` never called anywhere. Initially looked alarming (a
completely unused savestate class would mean rollback isn't wired up), but
tracing further found the real mechanism: `EXIBrawlback.cpp`'s
`SaveState()`/`handleLoadSavestate()` call `IncrementalRB::SaveWrittenPages()`/
`IncrementalRB::Rollback()` (`#include <incremental-rollback/incremental_rb.h>`),
a separate, page-dirty-tracking incremental savestate system under
`Core/Brawlback/include/incremental-rollback/`. This lines up exactly with
this branch's name (`savestate-efficiency`) - `BrawlbackSavestate` is the
original Slippi-derived full-region-copy approach, superseded by a more
efficient incremental one, with the old class simply left in the tree
unused rather than deleted. Worth remembering this if `BrawlbackSavestate`
ever looks like a lead again - it isn't one.

**`TimeSync.cpp`'s public functions are all genuinely wired up** - checked
`shouldStallFrame`/`startGame`/`TimeSyncUpdate`/`ReceivedRemoteFramedata`/
`ProcessFrameAck`/`getMinAckFrame` against `EXIBrawlback.cpp` and every one
has at least one real call site. No dead-code leads here.

**Deliberately did not dig into `IncrementalRB`'s internals**
(`incremental_rb.cpp`, `mem.cpp`, `job_system.cpp`, `tiny_arena.cpp`) or the
actual timing/stall logic inside `TimeSync.cpp`'s functions. This is the
highest-risk area in either repo to guess at: a custom page-tracking
allocator and frame-timing algorithm, not simple glue/wiring code like the
bugs fixed earlier today. A wrong "fix" here could cause silent memory
corruption or subtle desyncs that are far harder to notice or diagnose than
a wrong costume or stage - exactly the kind of mistake this session already
made once with #73 and had to revert. This needs either the original
author's context or a live two-machine test to safely touch, not static
reading alone. Flagging clearly rather than leaving it looking
unexamined.

### 2026-07-29: third instance of the dead-code-vs-live-code pattern - login flow, deliberately not touched

While tracing the playkey path bug (below), found that `containers/Header/index.tsx`
has a complete, working-looking login flow (`ActivateOnlineDialog`/
`ActivateOnlineForm`, wired to the real `usePlayKey`/Firebase pipeline) - but
grepping for its importers shows it's **only ever imported by `MainView.tsx`**,
already confirmed dead code earlier this session (never rendered by the live
app). The actual live header, `pages/base/UserHeader.tsx`, has a "Log in"
button that's a `console.log("login")` no-op - same shape as the Settings-tabs
find and the MainView/Replay find, a third instance of this pattern in one
launcher.

**Deliberately not wired in, same reasoning as the Replay/Spectate case**:
the backend this login flow talks to (`slippiBackendService`'s Firebase
config + `SLIPPI_GRAPHQL_ENDPOINT`) is Slippi's real infrastructure, not
Lylat's - there's no Lylat-equivalent config anywhere in this repo, and
that's third-party information this session has no access to. Checked
whether wiring it in anyway could be actively harmful (e.g. silently
submitting real login attempts to Slippi's actual production servers) -
it can't be: `.env.example` only has placeholder `"example"` values, no
real `.env` exists in this checkout, so `SLIPPI_GRAPHQL_ENDPOINT`/Firebase
config would be `undefined` at runtime and the flow would simply fail/throw
rather than hit anything real. Still not fixed, though, since a
wired-in-but-broken login dialog is worse than the current honest
no-op stub - same judgment call as Replay/Spectate/Console above. Whoever
gets Lylat's real backend details (its own API, or its own Firebase
project) can wire `containers/Header/index.tsx`'s `ActivateOnlineDialog`
into `pages/base/UserHeader.tsx`'s login button directly - the UI is
already built, it just needs a real backend to point at.

### 2026-07-29: real launcher bug found - playkey file written to a path Dolphin never reads

While checking whether unranked matchmaking needs a real account, traced
`CEXIBrawlback::getUserInfo()` (Dolphin clone) and found it reads
credentials from a `lylat.json` file in Dolphin's own user-data directory
(`File::GetUserPath(D_USER_IDX) + "lylat.json"`, or
`File::GetExeDirectory() + "/lylat.json"` on Windows). The launcher's
`writePlayKeyFile`/`findPlayKey` (`src/dolphin/playkey.ts`) wrote to a
completely different file: `user.json` inside Slippi's own directory names
(`~/Library/Application Support/com.project-slippi.dolphin/Slippi` on
macOS, `~/.config/SlippiOnline` on Linux). **A user could go through the
entire login flow and it would silently do nothing** - the key file would
exist, just not where Dolphin ever looks, so `getUserInfo()` would always
fall through to its "Could not find lylat.json" branch and return empty
credentials regardless of login state.

Verified the correct paths from source rather than guessing: this Dolphin
fork hasn't rebranded Dolphin's own user-directory names at all - grepped
`Common/CommonPaths.h`'s `NORMAL_USER_DIR` and `UICommon::SetUserDirectory()`
and found they're still literally `"Dolphin Emulator"` (Windows),
`"Library/Application Support/Dolphin"` (macOS), `"dolphin-emu"` (Linux) -
no Slippi or Brawlback/Lylat branding at any of these layers. **Fixed**
(launcher commit `a54ed54`): macOS now points at
`~/Library/Application Support/Dolphin`, Linux at `~/.dolphin-emu` (the
legacy/non-XDG default Dolphin uses when that directory already exists;
Dolphin's XDG fallback to `$XDG_DATA_HOME/dolphin-emu` when it doesn't is
**not** replicated - flagged as an unverified edge case, would need a real
Linux install to confirm which path applies). Windows was already
structurally correct (same directory as the Dolphin executable), just
needed the filename fix. The `PlayKey` TypeScript type's fields
(`uid`/`playKey`/`connectCode`) already matched what `getUserInfo()` reads -
only the file location was wrong. Typecheck-clean.

**Important context this fix surfaced**: the entire login/account backend
this launcher currently talks to (`slippiBackendService`, Firebase auth
config via `FIREBASE_API_KEY`/etc. env vars, `SLIPPI_BACKEND_URL` GraphQL
endpoint) is **unmodified Slippi infrastructure** - it doesn't point at
Lylat's backend at all, and there's no equivalent Lylat config anywhere in
this repo. This is a much bigger, out-of-scope gap (needs Lylat's actual
backend details, which live entirely outside these forked repos) - not
something this session can fix blind. **However, this does not currently
block unranked play specifically**: `Matchmaking::startMatchmaking()`
(Dolphin clone) already has its `if (!m_user->IsLoggedIn()) { ...
"Must be logged in to queue"... }` check commented out, and the launcher's
own `launchNetplay()` (`useDolphinActions.ts`) never checks login/playKey
state before launching - only Dolphin's install status. So matchmaking
will proceed with empty/anonymous `uid`/`playKey` regardless of login,
for any mode including unranked. Whether **lylat.gg's server** independently
rejects anonymous tickets even for unranked is the one remaining unknown
here, and it's a pure live-test question - can't be determined by reading
code on either side of this connection.

### 2026-07-29: audit pass toward "play unranked end-to-end via the launcher"

Goal reframed by the user: work toward being able to play at least unranked
through the launcher, jumping between blocked/unblocked areas freely, no
further check-ins needed until real PC/fork access. Spent this pass
auditing the actual launcher→Dolphin→matchmaking chain for anything that
would block unranked specifically, rather than more speculative Dolphin-side
edits (see the #73 mistake above - static C++ reasoning about multi-hop
network flows without build/live-test is exactly where this session already
went wrong once; being more conservative about it now).

**Checked, all clean, no bugs found:**
- Launcher's live Play button (`pages/base/PlayButton.tsx`, confirmed live
  via `AppBase.tsx`/`App.tsx` routing - there's a second, dead
  `components/play_button/PlayButton.tsx` used only by the unreferenced
  `MainView`, don't confuse the two) → `useDolphinActions.launchNetplay` →
  `dolphinService.launchNetplayDolphin()` → `DolphinManager.launchNetplayDolphin()`
  (`manager.ts`) → `DolphinInstance.start()` (`instance.ts`). No auth
  gating anywhere in this path (`AppBase`/`HomePage`/`UserHeader` don't wrap
  Play in any login requirement - login button is a `console.log` stub, but
  that's fine since Brawlback's account system, wherever it lives, isn't a
  launcher-side concern for unranked). CLI args passed to Dolphin are just
  `-b -e <isoPath>`, nothing Slippi-connect-code-specific left over - correct
  for Brawlback's architecture, since matchmaking happens in-game via EXI
  (`CMD_FIND_OPPONENT`), not launcher command-line args.
- `Matchmaking::getMMHostForSearchMode()` in the Dolphin clone - initially
  looked suspicious (always calls `getMexMMHost()`, which for non-dev builds
  returns `SConfig::GetInstance().m_slippiCustomMMServerURL`, a
  Slippi-inherited "user-configurable" field, instead of the hardcoded
  `MM_HOST_PROD`/`MM_HOST_DEV` constants - both dead code, never read).
  Traced further: `m_slippiCustomMMServerURL` is **only ever referenced in
  two places** (its `ConfigManager.h` declaration and this one usage site) -
  grepped all of `Core/Config/*.cpp` and found it's never loaded from any
  `.ini` file or exposed in any UI, so nothing can ever actually change it
  at runtime. Its compiled-in default is `"lylat.gg"` (`ConfigManager.h:63`).
  **Net effect: always connects to lylat.gg in practice, same as the
  constants would give you** - unwired groundwork (like several `m_slippi*`
  fields near it), not a live bug. Left alone; wiring it up for real would
  be scope creep with no evidence it's needed.
- The Dolphin auto-download/update pipeline (`fetchLatestVersion.ts`,
  launcher commit `f2f6cad`) was already fixed and **live-verified against
  the real network** earlier this session - resolves real Lylat Dolphin
  releases correctly.

**Current overall status for "unranked through the launcher"**, as best
this session can determine without an actual two-machine live test:
1. Launcher can download/launch the right Dolphin build with the right ISO. ✅ (verified as far as static analysis + live version-resolution check allows)
2. In-game menu → `CMD_FIND_OPPONENT` → Dolphin matchmaking against lylat.gg
   for `UNRANKED` mode specifically. ✅ protocol-correct after this session's
   #72 fix (mode byte parsed correctly; UNRANKED doesn't even touch the
   connect-code path, so it was arguably already fine before #72's fix too -
   #72 only really mattered for DIRECT/TEAMS modes).
3. Game settings merge/sync after a match is found. ⚠️ **#73 still open** -
   costume sync has a real, not-yet-diagnosed bug (see correction above).
   Doesn't block a match from starting, just costume display correctness.
4. Actual rollback gameplay once in-match: **not audited this pass** - out
   of scope for a launcher-focused pass, and this is the part most in need
   of a real two-client live test rather than more static reading.
5. Nothing found in the launcher itself that would prevent reaching a Play
   click and having Dolphin launch correctly.

**Bottom line**: nothing new is blocking unranked play from the launcher's
side specifically. The remaining known blockers are all pre-existing and
already documented above: #73 (costume sync, unresolved), the general "has
anyone actually live-tested a real rollback match on this branch" question
(needs two machines/the user's real PC), and everything is still sitting as
unpushed local commits pending the `skanderbm123` forks of `dolphin` and
`brawlback-launcher`.

## Working conventions established so far

- Git identity for commits: `skanderbm123` / `skander96@hotmail.com`.
- Commit small, working increments and push often — don't let a session end
  with only local, unpushed work (this was a hard lesson from the separate
  decomp project — sessions can get cut short unexpectedly).
- Don't invent new infrastructure/abstractions when the existing hook API
  already covers the need — e.g. the Stadium fix uses the *existing*
  `syInlineHookRel`/module-ID mechanism (already used for menu/scene RELs)
  rather than assuming stage-REL hooking needed new infrastructure. It
  didn't; the module-ID scheme is generic across all REL types, keyed off
  Brawl's own REL header `id` field, not a Brawlback-specific concept.
- Prefer small, surgical fixes over full 1:1 ports of Slippi's multi-file
  solutions when a smaller Brawl-specific fix achieves the same effect (see
  Stadium fix reasoning above: 1 hook instead of 3 ported files).
- Raw address/offset constants (not shared struct headers) are the
  established idiom in this repo for referencing game structures from hook
  code — see `#define P1_CHAR_ID_IDX 0x98` etc. at the top of
  `Rollback_Hooks.cpp`. Don't try to `#include` the separate `brawl` decomp
  repo's headers into this repo; they're different projects with different
  build systems. Use the decomp repo only as *reference* (to compute correct
  offsets/addresses), then hardcode the result here.
- **Building on Linux works now** (confirmed 2026-07-23) but needs the
  submodule symlink workaround noted above (case-sensitivity bugs in the
  external `BrawlHeaders` repo) — that workaround is deliberately *not*
  committed here since it's not our submodule to patch. If a future session
  is confused why a clean clone fails on `revolution/fa/...` includes, this
  is why; redo the two symlinks from "To build/test this" above.
