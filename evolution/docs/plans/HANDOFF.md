# Handoff — EVOLUTION (uppdaterad 2026-10-24, steg 3 klart; helgrenens granskning av steg 3 återstår)

Läs detta först i nästa session. Specen och planerna ligger bredvid.

## Läge

- **Spec:** `2026-10-21-evolution-design.md` — godkänd av ägaren. Alla standardval i avsnitt 9 gäller (Vulkan 1.3 + RT krävs, Studion, DOS-boot, två epoker, CRT på, engelsk text, syntetisk placeholder-musik). Credits-texten är inte bekräftad. CREDITS (kapitel 29) är sedan steg 2 ett 1993-kapitel: en sinusscroller med `eraFixed` 0 (se steg 2 nedan).
- **Planer:** `2026-10-21-step0-skeleton.md` (steg 0, klart), `2026-10-23-step1-audio-timeline.md` (steg 1, klart; amendment med tre akter), `2026-10-24-step2-retro-pipeline.md` (steg 2, klart; slutgranskat med en fixvåg), `2026-10-25-step3-retro-effects.md` (steg 3, klart; slutgranskning återstår), `2026-10-26-step4-rt-foundation.md` (steg 4, utkast pågår). Status per task står längre ner här.
- **Branch:** `claude/evolution-showcase-plan`, utkast-PR #3 i jojo-swe/PulseAscendant. Allt arbete under `evolution/`.
- **Musik:** levererad. Tre Suno-låtar av joyolo, en per akt: Amiga Trance Demo (194,00 s), Guitar Trance (218,40 s), Rigid Arpeggios (298,76 s), totalt 711,16 s. MP3:orna i `music/source/` och WAV:arna i `assets/music/` är git-ignorerade; `python3 tools/prepare_show.py "music/source/Amiga Trance Demo.mp3" "music/source/Guitar Trance.mp3" "music/source/Rigid Arpeggios.mp3"` återskapar `show.wav`, headers och kvitton. Varje aktskarv i `show.wav` har en 3 ms raised-cosine-fade (tar bort ett klick; längd, analys och kapitelstarter oförändrade). Proveniens i `licenses/MUSIC-NOTICE.md`. Placeholder-låten används bara av Python-testerna.
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

**Till steg 2, första task (från slutgranskningen):** `Offscreen`/`Context::immediate()` är en återvändsgränd — bygg en frame-ring med timeline-semafor, en `Image`-typ som spårar layout och genererar barriärer (delad `gfx/sync.hpp`), pass som funktioner, readback endast vid export. Skapa context före fönstret (ingen blinkande ruta vid exit 2). Övriga uppskjutna småfynd: `--duration` saknar övre gräns; Debug/Release delar `bin/` på VS; windowed saknad loader → exit 1 via SDL; ~~float-tid i push constants (precision vid 5 min)~~ (löst i steg 2: shaders får kapitellokal tid, `localSeconds` och `retroSeconds`); JSON-skrivare för `--show-info`; PNG-writer håller tre kopior; multi-track `direct_show` otestad; `python_tools` saknar TIMEOUT.

## Steg 1 — KLART (2026-10-24, plan `2026-10-23-step1-audio-timeline.md`)

| Task | Status | Commit |
|---|---|---|
| 1. Python-pipeline + placeholder-låt | **klar**, granskad | d29aee7 |
| 2. WAV-läsare + transport | **klar**, granskad | d0039ea |
| 3. Cue-sheet + timeline | **klar**, granskad; ommålad till tre akter | b53f227; musikbytet 2f04ad3, b001965, 1cb89c6, ac4832b, 2c2b2dd, 7ddc231 |
| 4. Era/övergångar | **klar**, granskad (en fixrunda) | 5840f24, bf8d8be |
| 5. SDL3-spelare + klocka | **klar**, granskad | 9c809b9 |
| 6. Frame synlig i testmönstret | **klar** | f726216 |
| 7. README + handoff | **klar** | `Document the music pipeline and step 1 controls` |

15/15 CTest gröna på lavapipe vid steg 1:s slut (dagens lista står under steg 2), plus `windowed` och `silent_mode` bakom `EVO_RUNTIME_TESTS`. Bygget renderar fortfarande testmönstret, men det visar timelinens läge (kapitel, epok 1993/2026, övergång, beat och kick).

Beslut i steg 1: `direct()` har `max_chapter_seconds` (40 för EVOLUTION, 18 default); exakta beat-tider (4 decimaler); `show_targets.json` är ett objekt `{max_chapter_seconds, chapters:[…]}` — Task 3 cross-check läser `.chapters`/`chapterTargets[]`. Placeholder: hi-hats på fjärdedelar (åttondelar fick tempo-estimatorn att läsa 2/3-tempo). De riktiga trance-låtarna lästes också på 2/3-tempo (3/16-delays); `beat_grid.disambiguate_period()` väljer nu tempo efter basonseter på kandidatgridden (Amiga Trance Demo 141,6, Guitar Trance 140,5, Rigid Arpeggios 138,3 BPM; placeholdern oförändrad 134). Showen är tre akter, 31 kapitel, 34 135 680 frames = 711,16 s (planens amendment). BeatSwap-pariteten är förankrad i downbeat D (flippen på D landar alltid på 2026). `--show-info` skriver musiksökvägen `assets/music/show.wav` relativt exe:n, samma som `--music`-defaulten. Configure länkar `assets/music/show.wav` till `bin/assets/music/` när den finns (kopia på Windows); annars SILENT MODE. Klockan faller tillbaka till monoton tid om strömmen dör mitt i showen. `chapterForKey`/`windowTitle` är publika för tester. `silent_mode` spelar en gles tyst WAV av showens exakta längd, så bara ljuddrivrutinen avgör.

**Uppskjutna småfynd från steg 1** (ledgern försvinner med containern; detta är listan):
- JSON i `showInfoJson`: locale (imbue classic) och escaping av titlar.
- Accentens övre gräns kan slå fel på en ulp (jämför step/rate <= .76*duration).
- Samplingsfrekvensen (48 kHz) står på flera ställen.
- ~~`effectNames` storlek ej assertad~~ — redan åtgärdat (`static_assert` i `cues.hpp`).
- WAV-läsarens felvägar saknar tester; `WAVE_FORMAT_EXTENSIBLE` avvisas.
- `Transport` har hög minnestopp (~1,5 GiB vid taket).
- ~~Byte till tyst läge mitt i showen rapporteras inte på stderr.~~ — åtgärdat i slutfixvågen (status skrivs vid varje byte).
- `positionSeconds` kan bli lika med längden (viks av `ShowClock`).
- ~~`clock_test` sätter SDL-hint med fel prioritet~~, ~~ordval om kapitel i `--help`/`app.hpp`~~, ~~multi-track `direct_show` otestad; `python_tools` saknar TIMEOUT~~ — åtgärdat i slutfixvågen, liksom UTF-8-sökvägar på Windows, enhetsbuffertens latens och `--audio-offset`.
- DP-trackern kan hamna ur fas (rätt tempo, fel fas) på material med hi-hat på varje slag plus 3/16-eko (`test_hat_echo_on_every_beat_keeps_the_tempo` testar bara tempot).
- När D kollapsar på kapitlets första beat blir det ett klipp utan flash (bara syntetiskt).
- Raytraces bar-flippar följer trackerns etiketter (downbeat-lucka vid beat 298 flyttar dem ett slag).
- ~~`eraCut` syns inte i testmönstret (ingen plats i push constants).~~ — löst i steg 2: compositorn blinkar `eraCut` över hela bilden.
- Övrigt från granskningarna: råa tag-bytes i WAV-felmeddelanden; testluckor (JSON-accent, `beatAt` efter sista beat); näst bästa tempokandidat ignoreras och `tempo_candidates` kan få nyckelkollisioner; ac4832b bygger inte ensam (bara bisect); `marks()`-hjälparen i era-testet speglar koden; wipe-fallbacken testas bara syntetiskt.

## Steg 2 — KLART (2026-10-24, plan `2026-10-24-step2-retro-pipeline.md`)

| Task | Status | Commit |
|---|---|---|
| 1. Render-grund (Image, Buffer, FrameRing, ComputePipeline, Readback) | **klar**, granskad (en fixrunda) | e676fd4, b13f2d9, 0cd7e88 |
| 2. Renderer, retro-steg, CRT-pillarbox, compositor | **klar**, granskad | e972532, 1036625 |
| 3. 8×8-font + textpass | **klar**, granskad | 3299146 |
| 4. BOOT | **klar**, granskad (en fixrunda) | 7417740, ad31bdc, cfc78e0 |
| 5. PLASMA | **klar**, granskad | c2dcb87 |
| 6. Sinusscroller | **klar**, granskad | ea05f70, ed9fb5b |
| 7. Storyboard-verktyg + README | **klar** | `Capture a storyboard of every chapter in both eras` |

23/23 CTest gröna på lavapipe (`options, png, png_decode, audio_transport, timeline, era, clock, context, gfx_foundation, renderer, boot, plasma, scroller, font, text_pass, swapchain, render_stream, capture, storyboard, show_info, exit_codes, no_wait_idle, python_tools`), plus `windowed`, `windowed_unsupported` och `silent_mode` bakom `EVO_RUNTIME_TESTS`. Testet `offscreen` finns inte längre (Offscreen ersattes av frame-ringen i Task 1–2).

Beslut i steg 2 Task 7 (storyboard): `tools/storyboard.py --app EXE --out DIR [--width 640 --height 360] [--chapters 0,3] [--no-crt]` fångar varje kapitel vid 20 %, accenten och 80 %, en gång tvingat till 1993 och en gång till 2026, och sätter ihop en PNG per akt (`act1.png`–`act3.png`; rader = kapitel, sex kolumner, cellerna nedskalade 2× med boxfilter, 2 px mörkgrå gutter, etiketter i ett eget 5×7-typsnitt eftersom HUD-titeln blir oläslig i halv storlek; HUD:en är ändå på). Tvingandet är den diagnostiska miljövariabeln `EVO_FORCE_ERA` (`0`/`1`, annat ignoreras) som `--capture-dir` och `--render-stream` läser (fönstret aldrig; sedan steg 3 Task 1, och en tvingad `--render-stream` skriver en rad om det på stderr): den sätter `era`, `wipe` (0 eller 1) och `eraCut` 0. Hela showen i 640×360 tar ~30 s på lavapipe. CTest `storyboard` kör kapitel 0 och 3 i 320×180 (i `build-out/storyboard_test` sedan steg 3:s slutfixvåg, så den aldrig tömmer `build-out/storyboard`) och kontrollerar storlek och att 1993- och 2026-cellerna skiljer sig; `capture` kontrollerar att `EVO_FORCE_ERA=1` ändrar BOOT och att ogiltiga värden ignoreras.

Beslut i steg 2 Task 6 (sinusscrollern): CREDITS (kapitel 29) har nu `eraFixed` 0 i stället för 1 — eftertexterna rullar som en 1993-sinusscroller (vit text på mörkblått `#000040`, statisk rad `EVOLUTION` överst), vilket aldrig hade syntes i en 2026-låst Cut. Kortet `1993 → 2026 · CREDITS` är flyttat till kapitlets thenCard så att HUD:en visar det (amendment-raden i steg 1-planen och `era_test` uppdaterade). Scrollern räknar positioner från `retroFrame / 35` (ren funktion, cachebar), en `text::Item` per tecken, och loopar när hela texten passerat (period 320 + textbredd). FLYBY behåller testmönstret till steg 3 men scrollar `WE WERE THERE IN 1993 ...` i index 255 (vitt i gråskalan) längst ner. HUD: under en Wipe visas båda epokkorten bara medan 0 < wipe < 1 (före: 1993-kortet, efter: 2026-kortet); ett kort som inte ryms i 2× mellan marginalerna ritas i 1×.

**Steg 2:s slutgranskning (Opus): godkänd med fixar** (fixvåg efter `fa69e1c`). Inga Critical. Åtgärdat:
- HUD:en skalar med utdatan i hela steg: `unit = max(1, round(höjd / 360))`; titel i `unit`, kort i `2·unit`, alla marginaler `8·unit`, skugga `unit`; ett kort som i `2·unit` skulle korsa marginalen ritas i `unit` (i 16:9 ryms 38 tecken i `2·unit`, så de fem korten på 40–43 tecken tar alltid `unit`). Kapiteltiteln är ett utvecklingsstöd och ritas bara i fönstret (`RenderSettings::hudTitle`); capture och `--render-stream` har bara epokkorten. Testat på CPU:n vid unit 1, 2, 4, 6 för alla kort, inklusive gränsen för fallbacken.
- Textbatchningen är en ren funktion (`text::splitBatches`) med CPU-test; `layout()` hoppar över icke-ändliga positioner/skalor.
- `renderer_test` kör två frames i flykt (PLASMA N/N+1 och PLASMA→BOOT) mot seriella renderingar; att tvinga paletten till en delad buffert får testet att fallera.
- Småfynd: `push(…, 0)` spelar inte in något och en pipeline utan bindings allokerar inga set/pooler (test med en fixtur-shader `shaders/test/no_bindings.comp`, kompilerad bara för testet); PLASMA använder `pc.retroSeconds`; swapchainen väljer B8G8R8A8_UNORM, R8G8B8A8_UNORM, annan UNORM, sist första formatet (`chooseSurfaceFormat`, enhetstest); FLYBY-hälsningen har skugga i index 0; `windowed_unsupported` kontrollerar exit-kod 2 (via `exit_code_test.py --windowed`); `no_wait_idle` täcker `src/render`, `src/text`, `src/retro` och flaggar också `immediate(`/`waitAll(` utan `// shutdown`, `// resize` eller `// setup`; kommentarerna i testmönstret och `app.hpp` rättade.
- Uppskjutet till steg 4 (granskningens Important 3): inget pass hoppas över när en epok inte syns (modern-steget körs även i en ren 1993-bild); blir steg 4:s första task, med semantik för historik-återställning.

**Rekommendationer till steg 3:** effektregistret i en `.cpp` (inte inline i `effects.hpp`, som drar in `boot.hpp`); alla nya retroshaders tar tiden från `pc.retroSeconds`; ett renhetstest över hela registret (samma retroFrame → samma bild, oavsett kick/tid inom framen); ett ctest som jämför `--render-stream` med `--capture-dir` för samma tid.

**Rekommendationer till steg 4:** en `DescriptorAllocator` som också klarar acceleration structures; `Image` med djup, mip-nivåer och SAMPLED för retro-färgbilden; modern-steget i HDR (RGBA16F) med tonemap; exporten med readback per ring-slot och RGB-packning på GPU:n; interaktivt läge mot exportläge (spp och seed); historik-återställning vid klipp och kapitelbyten. Ägaren ombeds köra en Windows-kontroll innan steg 3 bygger vidare.

**Uppskjutna småfynd från steg 2** (ur ledgern; överstrukna är åtgärdade i fixvågen):
- Render-grunden (Task 1): läsgenvägen i `Image::transition` spårar stage/access som separata unioner; `recording_` fastnar efter ett undantag mitt i en frame; `rgb()` före completion bara dokumenterat; ~~grep-testets luckor (`immediate(`/`waitAll(` och markörernas placering)~~; ~~`windowed_unsupported` matchar bara meddelandet, inte exit-koden~~; callback-listan kopieras vid varje `begin`; inget C++-test för `invalid_argument`-kasten; ~~push-storlek 0 når `vkCmdPushConstants`~~; ~~pipeline utan bindings skapar en tom pool~~.
- Renderer (Task 2): ~~inget test för två frames i flykt som överlappar~~; ~~kommentaren i testmönstret är inexakt~~; ~~`app.hpp`-kommentaren behöver radbrytas om~~. (CRT-barrelns ~3 kolumner hanteras av textmarginalerna ≥ 4 px; effekter utan shader rensar sin indexbild sedan Task 4.)
- Text/HUD (Task 3): ~~batchningen av överlappande text är inte låst av ett test~~; ~~ingen NaN-vakt i `layout()`~~; ~~titeln i 1× är pytteliten i 4K~~. ~~Kort upp till 41 tecken klipps under ~680 px~~ — åtgärdat i Task 6 (1×-fallback). ~~`RetroStage::text_` oanvänd~~ — används sedan Task 4. Kvar: i en mycket smal utdata (smalare än ett korts bredd i `unit`) klipps kortet fortfarande.
- BOOT (Task 4): `effects.hpp` drar in mycket via `boot.hpp`; en radstart en frame för tidigt fångas inte (testerna samplar 1–2 frames under).
- PLASMA (Task 5): flyttalsdrift mellan drivrutiner vid vikningsgränserna (ofarlig); ~~`kRetroRate` dubblerad i shadern~~; ingen kick-störning på GPU-nivå; namnen `palette.cpp` mot `palettes.cpp`; en lös kommentar; knyckar i C0-gradienten.
- Scrollern (Task 6): ~~FLYBY-hälsningen är svårläst över det ljusa testmönstret~~ (skugga i index 0; krocken med kortet i 640×360 består tills steg 3 byter mönstret); FLYBY-testet kontrollerar bläck och skugga, inte att mönstret överlever; ~~gränsen för 1×-fallbacken saknar test~~; planens Task 6-text är ersatt av besluten.

Steg 1:s slutgranskning (Fable) godkände grunden efter en fixvåg: 3 ms raised-cosine-toning vid aktskarvarna (ägaren informerad), UTF-8-sökvägar och argv på Windows, rapport vid byte till tyst läge, kompensation för enhetsbuffert plus `--audio-offset MS` (positivt = bilden tidigare relativt ljudet), testhärdning. Uppskjutet från den: BeatSwap i Dots får bara en bar-flipp innan beat-flipparna (överväg att förlänga fönstret i stället för att släppa en flipp, steg 6); WAV i int16 för halva minnet; RIFF-storlek 0xFFFFFFFF ger otydligt fel; exportens bildrutenät (711,16 s är inte heltal 60 fps-rutor — steg 8: padda ljudet till rutnätet); ägarkontroller på Windows med hörlurar (skarvar, A/V-offset, urkoppling av hörlurar). Från steg 2 Task 1: läsgenvägen i `Image::transition` spårar stage/access som separata unioner; `recording_` fastnar efter undantag mitt i en frame; `rgb()` före completion bara dokumenterat; grep-testets luckor (`immediate(`/`waitAll(`); `windowed_unsupported` matchar bara meddelandet; push-storlek 0; pipeline utan bindings.


Render-grunden (steg 0-punkterna) är nu på plats i Task 1: frame-ring med timeline-semafor, `Image` med layoutspårning och delad `gfx/sync.hpp`, compute-pass, readback bara vid export, context före fönstret. Övriga uppskjutna småfynd från steg 0 och 1 tas när de berör en task.

Steg 2 levererar: indexerad 320×200 med palett och palettcykling på CPU:n, fps-lås per effekt (retroFrame), 4:3-pillarbox med CRT (C / `--no-crt`), era-compositor, 8×8-font med HUD (H / `--no-hud`), BOOT, PLASMA och sinusscrollrar (CREDITS, FLYBY-hälsningen). Dithering finns som delad 4×4 Bayer-tröskel i `shaders/retro/common.glsl` (testmönstret använder den), inte som eget pass.

## Så här återupptas arbetet

1. Installera miljön (ovan), bygg och kör `ctest` i `evolution/` — allt ska vara grönt (35 tester). Lägg MP3:orna i `music/source/` och kör `prepare_show.py` (se Musik ovan) om du vill höra showen; konfigurera om efteråt så att `show.wav` länkas in.
2. Steg 3 är klart (se nedan) utom helgrenens slutgranskning (Opus) med fixvåg; kör den först, över `0715043..HEAD`. Låt ägaren köra Windows-kontrollen och titta på storyboarden (`python3 tools/storyboard.py --app build/linux/bin/Evolution --out build/linux/build-out/storyboard`, ~30 s på lavapipe).
3. Sedan steg 4 enligt `2026-10-26-step4-rt-foundation.md` (utkast pågår; gör klart och låt ägaren godkänna), med `bash <superpowers>/skills/subagent-driven-development/scripts/task-brief <plan> <n>`, implementer (Opus) + reviewer per task. Ledgern i `.superpowers/` är git-ignorerad och försvinner med containern; den här filen är ledgerns ersättning — uppdatera den vid sessionsslut.

## Steg 3 — KLART (2026-10-24, plan `2026-10-25-step3-retro-effects.md`)

| Task | Status | Commit |
|---|---|---|
| 1. Registret i `effects.cpp`, scen-providers, polygon- och punktrasterisering, determinismtester | **klar**, granskad (en fixrunda) | 0094297, 0fe4b21; fixar a4cd032 |
| 2. FLYBY (flatskuggat skepp över stjärnfält) | **klar**, granskad (två fixrundor) | 2938f11; fixar d6f9c9e, cbaa9fa |
| 3. DOTS (512 punkter som morfar) | **klar**, granskad (en fixrunda) | 7ce5029; fixar a489a18 |
| 4. TUNNEL (punktringar → massivt rör) | **klar**, granskad (en fixrunda) | a2550fe; fixar cbaa9fa, 0b473bb |
| 5. GLENZ (Future Crews glenzvektor över copper) | **klar**, granskad (en fixrunda) | d9df900; fixar da66f17 |
| 6. ROTOZOOM/LENS | **klar**, granskad | 1fbfd47 (merge aa2ded8); småfixar 0b473bb |
| 7. RAYTRACE ("förberäknad", 10 fps) | **klar**, granskad (en fixrunda) | 6f445c9; fixar 0b473bb |
| 8. VECTOR CITY och FINALE | **klar**, granskade (CITY:s fynd i slutfixvågen) | 1f4477d (merge c4247d5), 0880e7d; FINALE-fixar 0b473bb |
| 9. PORTAL, REBOOT, THE END | **klar**, granskad (en fixrunda) | 62020a4; fixar 0b473bb |
| Fixvåg (registret med designated initialisers m.m.) | **klar** | 0b473bb (merge 42ccd5b) |
| 10. Storyboard, README, HANDOFF | **klar** | `Close step 3 with fresh storyboards, README and handoff` |

35/35 CTest gröna på lavapipe (`options, png, png_decode, audio_transport, timeline, era, clock, context, gfx_foundation, renderer, boot, plasma, flyby, dots, tunnel, city, glenz, raytrace, finale, closing, rotozoom, scroller, raster, retro_registry, font, text_pass, swapchain, render_stream, capture, stream_capture, storyboard, show_info, exit_codes, no_wait_idle, python_tools`), plus `windowed`, `windowed_unsupported` och `silent_mode` bakom `EVO_RUNTIME_TESTS`. Varje kapitel med en 1993-sida (akt I, REBOOT, CREDITS, THE END) visar nu sin effekt; akt II och akt III:s planetkapitel är bara 2026 och visar testmönstret i grått om de tvingas till 1993. 2026-sidan är fortfarande platshållargradienten.

Steg 3 levererar: CPU-transform + GPU-rasterisering på 486-vis (scenen transformeras i `double` per retroFrame, klipps mot närplanet z ≥ 0,1 och ett guard band 512 px utanför varje skärmkant, projiceras med pixelaspekt 1,2 och kvantiseras till 28.4 fixpunkt med `std::lround`); `retro/polys.comp` (en invocation per pixel, heltalskantfunktioner med top-left-regeln, vattentätt, lägen flat/gouraud/glenz; gouraud exakt i heltal med Bayer-dither) och `retro/points.comp` (kvadratiska punkter, senare vinner via en ordningsbuffert); max 2048 trianglar och 8192 punkter per retroFrame, kontrollerat varje frame (`retro::checkScene`, `SceneRaster` kastar; `retro_registry` kör varje retroFrame av varje effekt). `EffectInfo` har fått `scene`, `texture` (effektens egen R8_UINT-textur, byggd en gång; LENS) och `resolve` (FINALE). Effekterna: FLYBY, DOTS, TUNNEL, GLENZ, LENS, RAYTRACE, VECTOR CITY, FINALE, PORTAL, REBOOT och THE END (beskrivna i README).

**Beslut i steg 3:**
1. Guard-bandets undantag behålls: `SceneRaster` kastar `invalid_argument` för primitiver utanför bandet. Bara handbyggda skärmprimitiver kan träffa det (`addTriangle`/`addPoint` klipper alltid).
2. `EVO_FORCE_ERA` gäller både `--capture-dir` och `--render-stream` (aldrig fönstret); en tvingad `--render-stream` skriver `render-stream: EVO_FORCE_ERA=<v> (<era> only)` en gång på stderr, eftersom exportvägen annars tyst kunde bli fel. `--show-info` ger varje kapitel `"eras"`, som `stream_capture` använder: varje kapitel med 1993-sida jämförs tvingat på båda epokerna plus otvingat (mitt i, och vid 0,3 för icke-Cut).
3. FLYBY ritar skeppet i fast målarordning från separerande plan (en liten BSP i modellrymden) i stället för `sortBackToFront`. Tre kända begränsningar är dokumenterade i `flyby.hpp` och har mätta budgetar i testerna: vingroten sjunker in i flygkroppen (en remsa kan synas), flamtrianglarna skär varandra (samma index, syns aldrig), och vyer under vingplanet nära mittplanet ordnar fel upp till ~1 enhet djupt (FLYBY:s flykt tar aldrig den grenen, |x| ≥ 0,42; PORTAL gör det i 8 frames, testat).
4. GLENZ: schackbitar 6/7 ovanpå koderna 1 (fram) och 2 (bak) i bit 4–5, som i originalet (`epolysb`); utan dem blir objektet en platt kod-3-klump. Överlapp är fortfarande `base | 3` i kodfältet. Alla 144 nåbara färgkombinationer är distinkta (testat). GLENZ får läsa spårets beat-grid via `show::beatGrid` (fast data per kapitel). Licensnotis i `licenses/FUTURE-CREW-NOTICE.md`.
5. RAYTRACE: 6×6×6-kub (16–231) plus 24 gråtoner (232–255); planens "40 grått" var en räknefel (skulle spräcka paletten); 0–15 orörda. Epokkortet säger `10 FPS` (verklig takt).
6. PORTAL:s och VECTOR CITY:s animationer är tajmade mot accenten, inte kapitlets slut (1993 syns bara till ungefär accenten); efter accenten håller PORTAL och CITY flyger vidare plant. CITY:s himmel är en gouraud-kupol i världsrymden (shadern vet inte kameran); i snitt 300 stadspolygoner per frame (väggar, tak, gatuplattor plus ~45 fönster och mittlinjestreck), högst ~390; kortet säger därför `300 POLYGONS` och `city_test` håller snittet inom 250–350 (473–834 trianglar med himlen). Fönster och mittlinjestreck krymper in i diset före sina klippavstånd (90 resp. 120), så de aldrig poppar.
7. FINALE klipper var **annan beat** (inte varannan takt: kapitlet är ~7 takter, och BeatSwap visar bara 1993 på sex enskilda slag), med en vit flashframe per klipp (`kFinaleBeatsPerCut` i `finale.hpp`). Mekanismen är `EffectInfo::resolve`: varje retroFrame lämnas till deleffektens registerpost med en syntetisk TimelineFrame från deleffektens HEMKAPITEL (längd, accent, beats) och ett hemlokalt retroFrame, så deleffekterna beter sig exakt som i sina egna kapitel; cachen nycklas fortfarande på FINALE, texten tas från FINALE (ingen FLYBY-hälsning i megamixen). Att flashen nästan inte syns under BeatSwap är accepterat (compositorns `eraCut`-blink täcker).
8. Registret använder designated initialisers (`.shader = …, .scene = …`).
9. Parallellt arbete i git worktrees (se nedan); granskningar batchade och körda parallellt med nästa implementer.
10. FLYBY-hälsningen ligger på baslinje 172 (var 180), så den aldrig korsar 1993-kortet; `scroller_test` kontrollerar det i 640×360 till 3840×2160 (Task 10, enda fixen efter storyboard-granskningen).
11. Slutfixvågen: `renderer_test` och `boot_test` använder GALAXY (akt III, aldrig någon 1993-sida) i stället för STUDIO NIGHT som testmönster och kontrollerar att dess post är testmönstret, eftersom steg 4 ger STUDIO NIGHT DOS-viloskärmen. `show::kEffectCount` ersätter `TheEnd + 1`. Delade hjälpare: `retro/math.hpp` (`radians`, `smoothstep`, `unit`), `retro::viewBasis` (scene, CITY:s frustum, `tests/ray_cast.hpp`), `tests/test_env.hpp` (`setEnv`; `renderer_test` har kvar sin kopia tills steg 4 Task 1 är sammanslagen). `city_test` jämför målarordningen mot en oberoende referens (slab-test mot `cityLot`-lådor och marken) och flyger svepet av accenter 5–40 s.

**Uppskjutna småfynd från steg 3:**
- THE END: epokkortet förblir vitt medan texten tonar ut (tona HUD:en med slutskärmen) — steg 6.
- FINALE: att hoppa över omrendering när den upplösta trippeln (effekt, kapitel, retroFrame) är oförändrad gjordes inte (ändrar vad `render()` returnerar och palettslottarna).
- `polys.comp` har ingen tile-binning (pixlar × trianglar); värsta fallet 2048 stora trianglar är inte tidmätt. Räcker för dagens högst ~930 (TUNNEL-röret).
- Klipparens kanoniska kantordning är defensiv och otestad (slumptester träffar aldrig en avrundningsgräns).
- Palettbufferten är en per slot: två `RetroStage::render` i samma inspelning delar sista paletten (äldre än steg 3).
- ROTOZOOM: `round()` vid .5 och float-sin/cos är inte bitexakta mellan GPU-leverantörer (testerna tål det). RAYTRACE: shaderns float-fasreduktion skiljer ~1e-6 mot CPU-tvillingen; ordnad dither syns som fin textur i himlen; lågmättade färger hamnar ibland på en gråton.
- DOTS: flödesgränserna (0,52 / 0,78) är rimlighetsgränser; x-snurren är vald för kapitlets tajming. GLENZ: copperbarerna är speglade parvis.
- `stream_capture` tar ~30 s och `python_tools` 60–370 s beroende på last; `linux`-testpreseten kör 8 tester parallellt.
- Determinism gäller inom en build på en maskin: libm (`sin`, `atan2`, `pow`) kan skilja i sista biten mellan Linux och Windows, och shaderns float-matte mellan GPU:er, så en pixel vid en avrundningskant kan skilja mellan plattformar (README säger det).
- Fjärrgrenarna `claude/evolution-step3-rotozoom` och `claude/evolution-step3-wip-glenz-fixes` är inaktuella (sammanslagna resp. ersatta av da66f17) och kan tas bort.

## Nästa steg: steg 4

Planen `2026-10-26-step4-rt-foundation.md` (RT-grunden för 2026-sidan) skrivs. Den ska ta med rekommendationerna till steg 4 under steg 2 ovan (DescriptorAllocator med acceleration structures, `Image` med djup/mips/SAMPLED, HDR-modernsteg med tonemap, readback per ring-slot, interaktivt mot exportläge, historik-återställning vid klipp, och att hoppa över pass för en epok som inte syns). Ägarens Windows-kontroll (bygg + ctest med `windows`-preseten, och showen i fönster med hörlurar) väntar fortfarande och bör göras innan steg 4 bygger vidare.

**Worktrees.** Huvudträdet `/home/user/PulseAscendant` står på `claude/evolution-showcase-plan`; parallella implementers jobbar i `/home/user/wt-a` (gren `step3-a`) och `/home/user/wt-b` (`step3-b`), som återställs till huvudgrenen efter varje sammanslagning (`git worktree add /home/user/wt-a -b step3-a`). En worktree konfigureras på sekunder utan nedladdning genom att peka FetchContent på huvudträdets källor och länka in musiken:

```sh
cd /home/user/wt-a/evolution
mkdir -p assets/music && ln -sf /home/user/PulseAscendant/evolution/assets/music/*.wav assets/music/   # före configure, så att show.wav länkas till bin/
M=/home/user/PulseAscendant/evolution/build/linux/_deps
cmake --preset linux -DFETCHCONTENT_SOURCE_DIR_SDL3=$M/sdl3-src -DFETCHCONTENT_SOURCE_DIR_VOLK=$M/volk-src \
  -DFETCHCONTENT_SOURCE_DIR_VULKANHEADERS=$M/vulkanheaders-src \
  -DFETCHCONTENT_SOURCE_DIR_VULKANMEMORYALLOCATOR=$M/vulkanmemoryallocator-src
cmake --build --preset linux && ctest --preset linux
```

Två worktrees bygger och testar samtidigt utan problem, men lavapipe blir långsammare under last. Bara kontrollern slår ihop till huvudgrenen och pushar.

Fullständig logg finns i `.superpowers/sdd/2026-10-25-step3-retro-effects/progress.md` (rapporterna `task-*-report.md` bredvid), men den är git-ignorerad och följer inte med till en ny session; allt som behövs står ovan.
