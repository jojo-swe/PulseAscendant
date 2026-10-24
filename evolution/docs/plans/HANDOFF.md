# Handoff — EVOLUTION (uppdaterad 2026-10-21, sessionens slut)

Läs detta först i nästa session. Specen och planerna ligger bredvid.

## Läge

- **Spec:** `2026-10-21-evolution-design.md` — godkänd av ägaren. Alla standardval i avsnitt 9 gäller (Vulkan 1.3 + RT krävs, Studion, DOS-boot, två epoker, CRT på, engelsk text, syntetisk placeholder-musik). Credits-texten är inte bekräftad.
- **Steg 0-plan:** `2026-10-21-step0-skeleton.md` — sex tasks. Status per task står längst ner här.
- **Branch:** `claude/evolution-showcase-plan`, utkast-PR #3 i jojo-swe/PulseAscendant. Allt arbete under `evolution/`.
- **Musik:** ägaren levererar Suno-låtar senare. Bygg mot syntetisk placeholder tills dess (steg 1).
- **Arbetssätt ägaren vill ha:** denna session är hjärnan/kontrollern; Opus-subagenter gör de enklare tasks (implementation och granskning). Processen: superpowers:subagent-driven-development med en implementer + en reviewer per task.

## Slutleverans (ägarens beställning 2026-10-23)

När demot är klart: rendera **4K60** med exportkedjan och publicera **privat på YouTube** via vidIQ-kopplingen (`vidiq_video_upload`, privacy private). Containern kör Vulkan i mjukvara (lavapipe), så den fulla 4K60-renderingen görs på ägarens RTX-maskin med `tools/render_video.py`; ägaren laddar upp filen, sessionen publicerar. Kortare/lägre förhandsvisningar kan renderas här.

## Miljö (måste göras om i en ny container)

```sh
apt-get update && apt-get install -y mesa-vulkan-drivers vulkan-tools glslang-tools spirv-tools \
  libgl1-mesa-dev libegl1-mesa-dev libasound2-dev libpulse-dev libwayland-dev libxkbcommon-dev \
  libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libvulkan-dev
```

Ger lavapipe (Vulkan 1.4 i mjukvara med ray query, RT pipeline, mesh shaders), `glslangValidator`, `spirv-val`, `xvfb-run`.
GitHub nås bara via **git** (FetchContent med `GIT_REPOSITORY` + commit-hash), inte tarballs. SDL3-hämtningen tar några minuter.

Bygg: `cd evolution && cmake --preset linux && cmake --build --preset linux && ctest --preset linux --output-on-failure`

## Beslut (rulings) tagna av kontrollern

1. `evo_gfx` länkar `SDL3::SDL3-static` från Task 4 (paths.cpp använder `SDL_GetBasePath`), trots att Task 1 sa "länka inte SDL än". Kostar inget om fel.
2. `--capture-dir` och `--render-stream` sätter båda `headless = true`.
3. Task 1-commitens Co-Authored-By anger den modell som skrev koden (Opus); accepterat, ingen amend.
4. Task 1-implementerns tillägg: `--time` måste vara ≥ 0; tom `--capture-dir ""` är fel. Accepterat.
5. `VK_NO_PROTOTYPES` definieras efter beroendena (volk definierar den själv); om varningar dyker upp i senare tasks, flytta till `target_compile_definitions` på `evo_gfx`.

## Steg 0 — KLART (2026-10-23, HEAD c512605)

Alla sex tasks implementerade och granskade, slutgranskning av hela branchen (Fable) med en fixvåg som åtgärdade alla Important-fynd. 10/10 CTest gröna på lavapipe, validation layer aktiv i tester (exit 3 vid valideringsfel), exit 2 med läsbart meddelande på maskiner utan RT/loader/enhet. Exe ligger i `build/<preset>/bin/`, shaders i `bin/shaders/`.

Beslut i steg 0 (utöver tidigare): min upplösning 16; VK_KHR_swapchain via extraDeviceExtensions; render-finished-semaforer per swapchain-bild; `--headless` ensamt → exit 1; `--render-stream` kräver `--duration > 0`; debug messenger till stderr; Python-tester via `find_package(Python3)`; README anger CMake ≥ 4.2 för VS-generatorn; vk_check har egen namntabell (vk_enum_string_helper.h saknas i pinnade headers); testhook `EVO_TEST_REQUIRE_DEVICE_EXTENSION`; en levande Context åt gången (volk-globaler).

**Till steg 2, första task (från slutgranskningen):** `Offscreen`/`Context::immediate()` är en återvändsgränd — bygg en frame-ring med timeline-semafor, en `Image`-typ som spårar layout och genererar barriärer (delad `gfx/sync.hpp`), pass som funktioner, readback endast vid export. Skapa context före fönstret (ingen blinkande ruta vid exit 2). Övriga uppskjutna småfynd: `--duration` saknar övre gräns; Debug/Release delar `bin/` på VS; windowed saknad loader → exit 1 via SDL; float-tid i push constants (precision vid 5 min); JSON-skrivare för `--show-info`; PNG-writer håller tre kopior; multi-track `direct_show` otestad; `python_tools` saknar TIMEOUT.

## Steg 1 — pågår (plan `2026-10-23-step1-audio-timeline.md`)

| Task | Status | Commit |
|---|---|---|
| 1. Python-pipeline + placeholder-låt | **klar**, granskad | d29aee7 |
| 2. WAV-läsare + transport | **klar** | d0039ea |
| 3. Cue-sheet + timeline | **klar**; ommålad till tre akter | b53f227 + musikbytet (tempofix, tre låtar, 711,16 s) |
| 4. Era/övergångar | | |
| 5. SDL3-spelare + klocka | | |
| 6. Frame synlig i testmönstret | | |
| 7. README + handoff | | |

Beslut i steg 1: `direct()` har `max_chapter_seconds` (40 för EVOLUTION, 18 default); exakta beat-tider (4 decimaler); `show_targets.json` är ett objekt `{max_chapter_seconds, chapters:[…]}` — Task 3 cross-check läser `.chapters`/`chapterTargets[]`. Placeholder: hi-hats på fjärdedelar (åttondelar fick tempo-estimatorn att läsa 2/3-tempo). De riktiga trance-låtarna lästes också på 2/3-tempo (3/16-delays); `beat_grid.disambiguate_period()` väljer nu tempo efter basonseter på kandidatgridden (Amiga Trance Demo 141,6, Guitar Trance 140,5, Rigid Arpeggios 138,3 BPM; placeholdern oförändrad 134). Showen är tre akter, 31 kapitel, 34 135 680 frames = 711,16 s (planens amendment). Känt, ej åtgärdat: DP-trackern kan hamna ur fas (rätt tempo, fel fas) på material med hi-hat på varje slag plus 3/16-eko (`test_hat_echo_on_every_beat_keeps_the_tempo` testar bara tempot).

## Så här återupptas arbetet

1. Installera miljön (ovan), bygg och kör `ctest` i `evolution/` — allt ska vara grönt (3 tester).
2. Fortsätt steg 1 vid första task utan "klar" i tabellen ovan: `bash <superpowers>/skills/subagent-driven-development/scripts/task-brief evolution/docs/plans/2026-10-21-step0-skeleton.md 3`, implementer (Opus) + reviewer per task. Ledgern i `.superpowers/` är git-ignorerad och försvinner med containern; den här filen är ledgerns ersättning — uppdatera den vid sessionsslut.
4. När steg 0 är klart: slutgranskning av hela branchen, sedan steg 1 (ljud + timeline + placeholder-låt) får sin egen plan.
