# HANDOVER.md

Överlämning mellan agenter och sessioner. Läs innan du börjar, uppdatera innan du slutar.

## Aktuellt läge

*Senast inventerat 2026-11-04 av Claude (Cowork).*

Projekt: A standalone music-driven 3D demo for **Windows & Linux**.<br> Procedural worlds. Moving machinery. Stellar storms. Hardware ray tracing.

- Huvudbranch: `main`
- Senaste commit på `main`: 2026-11-04 `b91917a` Merge pull request #3 from jojo-swe/claude/evolution-showcase-plan
- Öppna PR:er: 1
- Andra brancher på remote: 35

## Pågående arbete

| Agent/vem | Branch | Status | Beskrivning |
|-----------|--------|--------|-------------|
| Claude | `claude/privacy-scrub-main` | PR #4 öppen, uppdaterad 2026-11-04 | Rewrite docs and metadata in neutral technical terms |
| Claude | `claude/evolution-showcase-plan` | branch utan PR, senaste commit 2026-11-04 | Merge main (Windows build workflow, release readme) into the showcase branch |
| Cursor | `cursor/privacy-guard-a5b2` | branch utan PR, senaste commit 2026-11-01 | Add privacy guard checks and mask home directory usernames |
| Cursor | `cursor/mask-evidence-home-user-07fd` | branch utan PR, senaste commit 2026-11-01 | Mask account names in evidence home-directory paths |
| jojo-swe | `evolution-build` | branch utan PR, senaste commit 2026-10-27 | Package the Windows zip outside the source folder's name |
| jojo-swe | `preview` | branch utan PR, senaste commit 2026-10-27 | Add the EVOLUTION show audio |
| jojo-swe | `release` | branch utan PR, senaste commit 2026-10-27 | 60 fps target, half-rate volume layer, export pipelines per feature set |
| jojo-swe | `perf-realtime-3` | branch utan PR, senaste commit 2026-10-27 | Document the third round: half-rate volume layer, 60 fps table, export ETA |
| jojo-swe | `perf-realtime-2` | branch utan PR, senaste commit 2026-10-26 | Give RAYTRACE, METROPOLIS, RAYTRACE INFINITY and ASCENT margin over 30 fps at 720p |
| jojo-swe | `act3-extended` | branch utan PR, senaste commit 2026-10-26 | Regenerate the byte baseline for the extended Act III track |
| jojo-swe | `perf-realtime` | branch utan PR, senaste commit 2026-10-26 | Merge the Act III music swap (7d99cc9) into perf-realtime |
| jojo-swe | `act3-music` | branch utan PR, senaste commit 2026-10-26 | Regenerate the byte baseline for Act III's new track |
| jojo-swe | `evolution-finish` | branch utan PR, senaste commit 2026-10-25 | Merge the showcase plan and spell the ASCENDANT waypoint |
| jojo-swe | `z-fixes` | branch utan PR, senaste commit 2026-10-25 | Document the build requirements, the Windows presets, TDR and the licence |
| jojo-swe | `step6-e4` | branch utan PR, senaste commit 2026-10-25 | Regenerate the baseline for the polished nebula, plasma and galaxy |
| jojo-swe | `step6-e3` | branch utan PR, senaste commit 2026-10-25 | Polish the glass and lens chapters: lighter piers, the floor shot over the fan, the city's horizon,  |
| jojo-swe | `step6-e1` | branch utan PR, senaste commit 2026-10-25 | Polish the Studio, DOTS, PARTICLE OCEAN and the Spheres chapters |
| jojo-swe | `step6-e2` | branch utan PR, senaste commit 2026-10-25 | Fly WORMHOLE through a lit tunnel and polish the city's chapters |
| jojo-swe | `step7-f` | branch utan PR, senaste commit 2026-10-25 | Tidy the boot text and the DOS listing, describe the epoch cards |
| jojo-swe | `step6-b2` | branch utan PR, senaste commit 2026-10-25 | Regenerate the baseline for the Studio's takes |
| jojo-swe | `step6-c` | branch utan PR, senaste commit 2026-10-25 | Regenerate the baseline for the balanced card lines |
| jojo-swe | `step6-b1` | branch utan PR, senaste commit 2026-10-25 | Cut between scenes per frame for the FINALE megamix |
| jojo-swe | `step6-d` | branch utan PR, senaste commit 2026-10-25 | Regenerate the baseline for the 1993 screens |
| jojo-swe | `step6-a` | branch utan PR, senaste commit 2026-10-25 | Regenerate the baseline for the bloom and the per-act look |
| jojo-swe | `step6-r` | branch utan PR, senaste commit 2026-10-25 | Merge branch 'claude/evolution-showcase-plan' into step6-r |
| jojo-swe | `step6-c0` | branch utan PR, senaste commit 2026-10-25 | Expose the transition marks and the CRT bulge |
| Claude | `claude/evolution-privacy-scrub` | branch utan PR, senaste commit 2026-10-25 | Rewrite documentation and comments in neutral technical terms |
| jojo-swe | `step5-d` | branch utan PR, senaste commit 2026-10-25 | Merge the regenerated baseline from main |
| Claude | `claude/evolution-step4-wip-export` | branch utan PR, senaste commit 2026-10-24 | WIP: step 4 task in progress (unfinished, untested) |
| Claude | `claude/evolution-step4-wip-studio` | branch utan PR, senaste commit 2026-10-24 | WIP: step 4 task in progress (unfinished, untested) |
| Claude | `claude/evolution-step3-wip-glenz-fixes` | branch utan PR, senaste commit 2026-10-24 | WIP: start the glenz review fixes (unfinished, untested) |
| Claude | `claude/evolution-step3-rotozoom` | branch utan PR, senaste commit 2026-10-24 | Rotate and zoom a logo texture under a bouncing lens |
| Claude | `claude/camera-movement-3d-assets-51cfbe` | branch utan PR, senaste commit 2026-10-16 | Calm the camera and rebuild hero models for v0.6.1 |
| Claude | `claude/cinematic-v0.6` | branch utan PR, senaste commit 2026-10-15 | Document v0.6 beat sync, camera rig and image pipeline |
| jojo-swe | `backup/local-main-v0.7-20261104` | branch utan PR, senaste commit 2026-10-13 | Document public v0.7 Short and full-film reference |

## Nästa steg

- Granska och besluta om de öppna PR:erna ovan (merga eller stäng).
- Gå igenom brancher utan PR: öppna PR, merga eller radera (fråga användaren innan radering).
- Fyll på "Bygga och testa" i AGENTS.md om något saknas eller är fel.

## Kända problem

- Inga dokumenterade ännu.

## Senaste commits

- 2026-11-04 `b91917a` Merge pull request #3 from jojo-swe/claude/evolution-showcase-plan
- 2026-11-04 `5d5195e` Merge main (Windows build workflow, release readme) into the showcase branch
- 2026-11-01 `59bb715` Add privacy guard checks and mask home directory usernames (#6)
- 2026-11-01 `8558d44` Merge pull request #5 from jojo-swe/cursor/mask-evidence-home-user-07fd
- 2026-11-01 `c88d085` Mask account names in evidence home-directory paths
- 2026-10-27 `8cebb61` Package the Windows zip outside the source folder's name
- 2026-10-27 `cc86a16` Also build the Windows zip on pushes to evolution-build
- 2026-10-27 `bac2b87` Build a Windows release zip of EVOLUTION on a tag
- 2026-10-27 `c983756` EVOLUTION: 60 fps target, half-rate volume layer, export pipelines per feature set
- 2026-10-27 `67274a0` Document the third round: half-rate volume layer, 60 fps table, export ETA

## Logg

| Datum | Agent | Vad som gjordes |
|-------|-------|-----------------|
| 2026-11-04 | Claude (Cowork) | Lade till AGENTS.md, HANDOVER.md och CLAUDE.md; inventerade PR:er, brancher och commits. |
