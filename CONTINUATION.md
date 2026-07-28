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
before real hook code can be written safely. This is a solid, evidence-backed
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

**The Dolphin-side fix, once `skanderbm123/dolphin` exists** (small, exact,
already verified against the real source — just needs someone to actually
apply + build it, this session couldn't push there):
```cpp
// In handleFindMatch(), replace the #ifdef/#else/#endif block with just the
// "then" branch, unconditionally:
Matchmaking::MatchSearchSettings search;
std::string connectCode;

search.mode = (Matchmaking::OnlinePlayMode)payload[0];
std::string shiftJisCode;
shiftJisCode.insert(shiftJisCode.begin(), &payload[1], &payload[1] + 18);
shiftJisCode.erase(std::find(shiftJisCode.begin(), shiftJisCode.end(), 0x00), shiftJisCode.end());
connectCode = shiftJisCode;
```
(Note: the original commented-out line says
`(SlippiMatchmaking::OnlinePlayMode)payload[0]` — that's almost certainly a
copy-paste leftover from porting Slippi's own code; the type actually in
scope is `Matchmaking::OnlinePlayMode`, defined right there in
`Matchmaking.h`. Double check this compiles before assuming the cast type -
this session couldn't build Dolphin to verify.)

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
5. Lower priority / less netcode-central: #76 (game-end/CSS-return workflow),
   #75 (pause workflow cleanup), #73 (P2 costume sync bug — related to #74),
   #71 (BrawlHeaders repo org migration), #70 (menu game-object reverse
   engineering).

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
