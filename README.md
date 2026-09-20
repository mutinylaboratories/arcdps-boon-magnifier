# arcdps Boon Magnifier

A Guild Wars 2 overlay that blows up one boon so you can't miss it. Right now that
boon is **Stability**: a big icon, a countdown until your last stack runs out, and
your stack count. One DLL works as an **arcdps extension** and as a **Raidcore
Nexus addon**.

## Features

- Large Stability icon: presets 48 / 64 / 96 / 128 / 192 / 256 px, or any size 24–384 px
- Countdown to when stability is completely gone (tenths of a second below 10 s)
- Stack count, a darkening sweep as time drains, red pulse + red digits below a
  warning threshold
- Crisp at any size: vector icon and segment digits by default (the game's own
  icon is only 32×32; it is available as an option, as is the host font)
- Countdown over, below, or right of the icon; opacity; dimmed icon while inactive
- Drag to place, then **Lock position** to make it click-through
- **Preview** toggle fakes stability so you can style it anywhere

## Install

Copy `arcdps_boon_magnifier.dll` next to your other arcdps extensions
(e.g. `Guild Wars 2\addons\`), or run `scripts\deploy.ps1` with the game closed.

Settings: **Nexus options → Boon Magnifier** when Nexus is installed, otherwise
arcdps options → Extensions → *boon magnifier*. They are saved to
`arcdps_boon_magnifier.ini` beside the DLL. The bottom of the options panel shows
where combat data is coming from.

### arcdps vs. Nexus

Nexus has priority: whenever it has loaded the addon it draws the overlay and
hosts the options menu.

| Setup | UI | Combat data |
|---|---|---|
| Nexus managing arcdps, shared `addons\` folder (both load the DLL) | Nexus | arcdps' direct callback. arcdps lists the extension but gets no options tab. |
| Nexus loads it, arcdps does not | Nexus | [ArcDPS Integration](https://github.com/RaidcoreGG/GW2-Arcdps-Integration) addon (`EV_ARCDPS_COMBATEVENT_SQUAD_RAW`), which must be installed. |
| arcdps only | arcdps | arcdps' direct callback. |

Disabling the addon in Nexus hides the overlay. Boon data always originates from
arcdps; Nexus itself has no combat API.

## Updates

The addon declares GitHub as its update provider, so **Nexus updates it by itself**: it
watches this repo's releases and installs the `.dll` attached to the newest tag that
outranks the installed version. Releases are produced by CI:

- push to `main` → `build` workflow (build + tests, artifact)
- tag `vX.Y.Z` (matching `CMakeLists.txt`) → `release` workflow publishes the DLL and the
  signature file
- the realtime source's signature is **compiled into the DLL** (`signatures/`), so a
  re-signed build after a game patch reaches users through the same update path; a file
  beside the DLL still overrides it

### After a Guild Wars 2 patch

The patterns are wildcarded and usually survive a patch, so the plugin scans the new exe
anyway and reports the result as *PROVISIONAL* in Diagnostics; only if a pattern no longer
matches does the overlay fall back to arcdps (nothing crashes). It also re-reads
`arcdps_boon_magnifier_sigs.ini` beside the DLL whenever the file changes, so a regenerated
signature takes effect without restarting the game. Two workflows keep it current:

- `gw2-watch` (GitHub-hosted, every 30 min) polls ArenaNet's CDN build id and opens an
  issue labelled `gw2-update` when it changes.
- `gw2-resign` (self-hosted runner tagged `gw2`, hourly) runs on a machine with the patched
  game and Binary Ninja: `tools/ci/resign.ps1` regenerates the signature, drops it into that
  machine's `addons\` folder (live within a second), bumps the patch version, commits and
  tags, and `release` takes it from there for everyone else. If a locator no longer
  finds its function, the job fails and the issue stays open: that is the case that needs a
  human with Binary Ninja (see `docs/gw2-buff-internals.md`).

Setting up the self-hosted runner: GitHub → repo *Settings → Actions → Runners → New
self-hosted runner* (Windows), follow the shown `config.cmd` steps and add the label `gw2`.
Register it at the repository (not org) level so only this repo's workflows can reach it.
Run it in the logged-on user's session rather than as a service: Binary Ninja's licence and
`python -c "import binaryninja"` are per-user. A scheduled task at logon that launches
`run.cmd` hidden does the job (`C:\actions-runner\start-runner.ps1` on the reference machine).

Because the runner is a real PC, fork pull requests must never run on it unapproved: the repo
sets *Actions → General → Fork pull request workflows → Require approval for all external
contributors*, and the workflow `GITHUB_TOKEN` defaults to read-only. Review a PR's workflow
changes before approving its run.

## Build

Requires Visual Studio with the C++ workload (its bundled CMake is used) and Ninja.

```powershell
.\scripts\build.ps1            # builds DLL + tests, runs them
.\scripts\deploy.ps1           # copies the DLL to the GW2 addons folder
```

Try the overlay without the game:

```powershell
.\build\Release\preview_host.exe .\build\Release\arcdps_boon_magnifier.dll
```

## Layout

- `src/core` – host-independent logic, unit-tested: stack tracking (`boon_tracker`),
  arcdps event decoding (`boon_events`), config (`config`)
- `src/plugin` – overlay/options UI, D3D11 icon texture, `entry_arcdps.cpp`, `entry_nexus.cpp`
- `tests` – doctest unit tests + `smoke_host.cpp`, built twice as headless fake hosts
  (`smoke_nexus` with ImGui 1.80, `smoke_arcdps` with 1.92.7) that load the real DLL
- `tools/preview_host.cpp` – D3D11 desktop preview acting as arcdps, with looping fake stability
- `tools/binja/` – Binary Ninja headless signature generator (see below)
- `third_party` – Dear ImGui **1.80** and **1.92.7**, Nexus API header, stb_image, doctest

## Two ImGui versions in one DLL

A plugin must be built against the same Dear ImGui version as the host whose context
it draws into, and the hosts differ: **Nexus ships 1.80, arcdps ships 1.92.7** (arcdps
refuses UI callbacks on mismatch: "imgui version mismatch: 18000 != 19270").

- 1.80 (`third_party/imgui`) is compiled normally and used by `entry_nexus.cpp` / `overlay_nexus.cpp`.
- 1.92.7 (`third_party/imgui192`) is compiled inside `namespace arc192` by
  `src/plugin/imgui192_unity.cpp` and used by `entry_arcdps.cpp` / `overlay_arcdps.cpp`.
  Its `imgui.h` carries one small marked patch (standard placement new) to allow that.
- The UI itself lives once in `overlay_impl.inl`, written against the API subset common
  to both versions and included by both overlay files.
- `state.*`, `icon_texture.*` and `src/core` never include ImGui.

When arcdps updates ImGui again, replace `third_party/imgui192` with the matching
release (re-apply the placement-new patch) and rebuild; same for Nexus and `third_party/imgui`.

## Why the overlay lags arcdps, and the realtime hook scaffolding

arcdps deliberately delays the combat feed it gives extensions by ~2.65 s ("intended
for statistics, not realtime notifications", per its API README); its local feed carries
no boon events at all (measured, see `Diagnostics`). So with arcdps alone the icon
appears ~2.65 s after the boon lands and lingers that long after a strip. The countdown
itself is exact once shown.

The plugin therefore has a **realtime source plug-in point**. Anything that knows the
local player's Stability state sooner calls `plugin::realtime_report(active, stacks,
remaining_ms)` (see `state.hpp`); `core/realtime_merge` then shows/hides the icon
immediately and fills in arcdps' durations as they arrive. Ships inert.

Scaffolding for a game-client hook, for people willing to do the reverse engineering:

- `tools/binja/make_sigs.py` (Binary Ninja headless) turns locator functions in
  `tools/binja/targets.py` (copy `targets_example.py`) into byte signatures with address
  operands wildcarded, checks each is unique, and writes
  `arcdps_boon_magnifier_sigs.ini` stamped with the exe's PE timestamp/size. After a game
  patch, rerun it; no rebuild. `--self-test` validates the generator on any exe.
- `src/core/sigscan` resolves those signatures in-process (unit-tested; verified end to
  end against `preview_host.exe`).
- `src/plugin/realtime_source` loads the ini from beside the DLL, refuses it if the exe
  build differs or any signature is missing/ambiguous, and installs the hooks declared
  in **`src/plugin/realtime_gw2.cpp`** through Nexus' MinHook, all-or-nothing.
- `realtime_gw2.cpp` implements a **polling source** for Stability: each frame it follows
  the pointer chain in `docs/gw2-buff-internals.md` from the game's thread-local context
  table to the local player's buff bar and reports stacks + remaining time. Pure reads
  through `safe_read`, no hooks, no game code executed. It only runs when the sig file
  resolved `[contexts_tls_index]` for the running exe build.

Regenerating after a game patch (needs Binary Ninja; reuse an analysed .bndb/.bnpr to skip
the long analysis):

```powershell
python toolsinja\make_sigs.py "C:\Program Files\Guild Wars 2\Gw2-64.exe" build\Releasercdps_boon_magnifier_sigs.ini --targets toolsinja	argets_gw2.py --enable
.\scripts\deploy.ps1     # copies DLL + sig file
```

If the structure offsets in `realtime_gw2.cpp` change, the poll will not find a buff bar
and the overlay silently falls back to arcdps (Diagnostics shows "nothing reported").

Read ArenaNet's third-party program policy before going down this road; the plugin's own
scope is deliberately limited to the local player's tracked buff.

## How tracking works

arcdps' area-combat callback reports buff stacks on the local player with a
trackable instance id. `BoonEventRouter` handles both the statechange encoding
(`CBTS_BUFFAPPLY/CHANGE/REMOVE_SINGLE/REMOVE_ALL`, `BUFFINITIAL`) and the older
`is_buffremove`/`is_offcycle` encoding, and subtracts the event delivery delay.
`BoonTracker` keeps per-stack timers; Stability stacks by intensity, so the
countdown is the longest remaining stack. Duration-stacking is implemented too,
ready for other boons.

Adding a boon = a buff id + stacking type in `state.cpp` and an icon.

## Credits

Stability icon © ArenaNet, used under the GW2 content terms of use.
Dear ImGui, stb, doctest, and the Nexus API header are under their own licenses in `third_party/`.
