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
