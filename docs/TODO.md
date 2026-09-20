# TODO

State as of 2026-09-19. Working in game: realtime Stability overlay (appears/disappears
instantly, countdown from game data), Hallowed Ground projection from the first pulse,
Stand Your Ground countdown, multi-boon overlays, Nexus auto-update from GitHub releases.

## Needs an in-game check
- [ ] **Squad window** (`show_squad`): never run in a squad yet. Watch `Nexus.log` for
      `debug: squad: N in roster, N in squad, N readable, N with boon, N attributed`.
      Attribution relies on `character+0x90` being the agent pointer (inferred from the
      character vtable slot 0 getter, not confirmed) — if "attributed" stays 0 while
      "with boon" > 0, that offset is the suspect.
- [ ] **Other boons**: duration-stacked remaining time (quickness/alacrity/protection: active
      stack ticks, queued stacks add their full duration) is implemented but unverified.
      Also confirm Nexus downloads the wiki icons (`Textures_GetOrCreateFromURL`).
- [ ] **Provisional Hallowed Ground projection**: gate is "HG on bar and not recharging";
      any other self-cast 2–6 s single stab stack would show ~10 s for up to 1.25 s. If seen,
      also require `buff->sourceAgent == own agent` (needs the agent pointer above).
- [ ] Nexus update flow end to end: cut v0.1.1 and confirm Nexus offers/installs it.

## Infrastructure
- [ ] Install the **self-hosted runner** (label `gw2`) on the gaming PC for
      `.github/workflows/gw2-resign.yml`; then dry-run `tools/ci/resign.ps1 -Force` once.
      Note it analyses the raw exe (10–20 min); could be taught to reuse a `.bnpr`.
- [ ] Delete the private `*-old` repos in the org once comfortable (needs `delete_repo`
      scope: `gh auth refresh -h github.com -s delete_repo`, or via repo settings).
- [ ] Re-enter Actions secrets in the recreated `ska` (4) and `o2ptima_companion` (2) repos.
- [ ] Bump `CMakeLists.txt` version and tag `v0.1.1` after the squad window is verified.
- [ ] README: add a screenshot; trim the diagnostics prose once things settle.

## Next features (in the order agreed)
- [ ] **Stab rotation view** (phase 3): timeline of squad stab coloured by source, own stab
      skill cooldowns underneath (recharge list is already read), "cast in N s" cue when
      the partner's stab will lapse before yours is ready. Partner cooldowns by inference
      (seen cast + known recharge) first; networking (Krappa-style relay) only if needed.
- [ ] Per-boon overlay size/position presets; maybe a compact "boon bar" layout.
- [ ] Vector renditions for the common boons (the wiki icons are 32 px and go soft when large).

## Housekeeping
- [ ] `kDumpRechargeList` and the recharge/squad `debug:` lines are diagnostics; drop or
      gate them once the layouts have survived a game patch or two.
- [ ] `tools/binja/*` scripts assume the `gw2.bnpr` project layout; document regenerating
      the `.bndb` after a patch (`annotate_gw2.py` addresses are build-specific).
- [ ] `docs/gw2-buff-internals.md` is the RE map; keep it in step with `realtime_gw2.cpp`
      offsets whenever one changes.
