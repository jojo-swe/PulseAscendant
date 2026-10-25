# Handoff — EVOLUTION (uppdaterad 2026-10-24, steg 4 klart och slutgranskat; Studio-polish och steg 5 härnäst)

Läs detta först i nästa session. Specen och planerna ligger bredvid.

## Läge

- **Spec:** `2026-10-21-evolution-design.md` — godkänd av ägaren. Alla standardval i avsnitt 9 gäller (Vulkan 1.3 + RT krävs, Studion, DOS-boot, två epoker, CRT på, engelsk text, syntetisk placeholder-musik). Credits-texten är inte bekräftad. CREDITS (kapitel 29) är sedan steg 2 ett 1993-kapitel: en sinusscroller med `eraFixed` 0 (se steg 2 nedan).
- **Planer:** `2026-10-21-step0-skeleton.md` (steg 0, klart), `2026-10-23-step1-audio-timeline.md` (steg 1, klart; amendment med tre akter), `2026-10-24-step2-retro-pipeline.md` (steg 2, klart; slutgranskat med en fixvåg), `2026-10-25-step3-retro-effects.md` (steg 3, klart; slutgranskat med en fixvåg), `2026-10-26-step4-rt-foundation.md` (steg 4, klart; slutgranskat med fixrunda E och fixvåg F). Status per task står längre ner här.
- **Branch:** `claude/evolution-showcase-plan`, utkast-PR #3 i jojo-swe/PulseAscendant. Allt arbete under `evolution/`.
- **Musik:** levererad. Tre Suno-låtar av joyolo, en per akt: Amiga Trance Demo (194,00 s), Guitar Trance (218,40 s), Rigid Arpeggios (298,76 s), totalt 711,16 s. MP3:orna i `music/source/` och WAV:arna i `assets/music/` är git-ignorerade; `python3 tools/prepare_show.py "music/source/Amiga Trance Demo.mp3" "music/source/Guitar Trance.mp3" "music/source/Rigid Arpeggios.mp3"` återskapar `show.wav`, headers och kvitton. Varje aktskarv i `show.wav` har en 3 ms raised-cosine-fade (tar bort ett klick; längd, analys och kapitelstarter oförändrade). Proveniens i `licenses/MUSIC-NOTICE.md`. Placeholder-låten används bara av Python-testerna.
- **Arbetssätt ägaren vill ha:** denna session är hjärnan/kontrollern; Opus-subagenter gör de enklare tasks (implementation och granskning). Processen: superpowers:subagent-driven-development med en implementer + en reviewer per task.

## Slutleverans (ägarens beställning 2026-10-23)

När demot är klart: rendera **4K60** med exportkedjan och publicera **privat på YouTube** via vidIQ-kopplingen (`vidiq_video_upload`, privacy private). Containern kör Vulkan i mjukvara (lavapipe), så den fulla 4K60-renderingen görs på ägarens RTX-maskin med `tools/render_video.py` (skrivs i steg 8; till dess `--render-stream` till ffmpeg); ägaren laddar upp filen, sessionen publicerar. Kortare/lägre förhandsvisningar kan renderas här.

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

1. Installera miljön (ovan), bygg och kör `ctest` i `evolution/` — allt ska vara grönt (50 tester, ~2 min med preseten som kör 8 åt gången; `python_tools` är långsammast). Lägg MP3:orna i `music/source/` och kör `prepare_show.py` (se Musik ovan) om du vill höra showen; konfigurera om efteråt så att `show.wav` länkas in.
2. Steg 4 är klart och slutgranskat (se nedan): Task 10:s granskning (fixrunda E) och helgrenens slutgranskning över `f4c91c2..fec6ce2` (fixvåg F) är åtgärdade. Kvar innan steg 5: den beslutade Studio-polishtasken (ljusval per punkt, brus vid källorna, tonemap, ljussättning per kapitel, himlen i 4K). Låt ägaren köra Windows-kontrollen och RTX-checklistan (steg 4 nedan) och titta på storyboarden (`python3 tools/storyboard.py --app build/linux/bin/Evolution --out build/linux/build-out/storyboard`, ~1 min 50 s på lavapipe vid standardvärdet `--spp 8`).
3. Sedan steg 5 (se "Nästa steg" längst ner): skriv planen, låt ägaren godkänna, kör den med `bash <superpowers>/skills/subagent-driven-development/scripts/task-brief <plan> <n>`, implementer (Opus) + reviewer per task. Ledgern i `.superpowers/` är git-ignorerad och försvinner med containern; den här filen är ledgerns ersättning — uppdatera den vid sessionsslut.

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

## Steg 4 — KLART (2026-10-24, plan `2026-10-26-step4-rt-foundation.md`)

| Task | Status | Commit |
|---|---|---|
| 1. Stegplan, stegskippning, historikåterställning | **klar**, granskad (fixar med Task 3) | ab30e09; fixar f2eceee |
| 2. Descriptors, RT-egenskaper, alignade buffertar, sync-validering | **klar**, granskad (fixrunda A) | 028f739 (merge 9992e39); fixar bf8854b |
| 3. Retrobilden som mipmappad SRGB-textur, djup och mips i `Image` | **klar**, granskad (fixrunda A) | 00b5d66 (merge c0f6814); planrättning 184dfec; fixar bf8854b |
| 4. Geometrilager, kompakterade BLAS, TLAS varje frame | **klar**, granskad (barriärerna handgranskade; småfixar i fixrunda C) | 04e72f3 (merge 50208b1); fixar 1efd0f2 |
| 5. RT-pipeline och beräknad SBT | **klar**, granskad | ce28f5a (merge 54fc056); rekursionsvakten i 95071fd |
| 6. Material, BSDF:er, delade GPU-structs, procedurella meshar | **klar**, granskad | 0cd88f6; planändring 2b5808d; fixar i 95071fd (rough glass) och 1efd0f2 (u-riktning) |
| 7. Path tracern (NEE/MIS, portal, retroskärm, guider) | **klar**, djupgranskad (fixrunda D) | 95071fd (merge fbab413); planrättning a386e14; fixar d15eaca |
| 8. Temporal ackumulering + à-trous-denoiser | **klar**, granskad (fixrunda A) | d5f5059 (merge 4bfe2b3); fixar ebdafaf (merge 9016455) |
| 9. HDR-modernsteg, scenregister, ACES-tonemap | **klar**, granskad (fixrunda C) | ce4d832 (merge 8bb2c5f); fixar 1efd0f2 |
| 10. Studion | **klar**, granskad (Opus; fixrunda E) | 77d3915, a2d3df6; fixar b35e31d (merge 1486b50) |
| 11. Export: `--spp`, seeds, exportdeterminism | **klar**, granskad (fynden gick till Task 10) | 9ea81d0 |
| 12. Seek-determinism, storyboard, README, HANDOFF | **klar** | fec6ce2 |
| Fixvåg F (helgrenens slutgranskning) | **klar** | `Close the step 4 review: docs, NaN checks, timeouts and the step 5 advice` |

50/50 CTest gröna på lavapipe (steg 3:s 35 plus `stage_plan, descriptors, accel, rt_pipeline, mesh_builder, material, denoiser, tonemap, path_tracer, compositor, image, export_mode, studio, seek_determinism, stream_capture_studio`), plus `windowed`, `windowed_unsupported` och `silent_mode` bakom `EVO_RUNTIME_TESTS`. Inga valideringsmeddelanden utom de två avsiktliga (`context`:s buffert av storlek 0 och `descriptors` SYNC-HAZARD). `accel`, `path_tracer`, `denoiser`, `image` och `descriptors` hazard-fall kör sync-validering (Beslut 9).

Steg 4 levererar: `evo_rt` (geometrilager, en kompakterad BLAS per mesh vid setup, TLAS varje frame ur instansbuffertar per ring-slot), RT-pipeline med beräknad SBT och en closest-hit per materialklass (pbr, glas, emissiv, schack), två miss (himmel, skugga), rekursionsdjup 1; path tracern i `evo_modern` (en raygen som loopar studsar, NEE mot rektangelljus av tre slag — Emitter, RetroScreen, Portal — med MIS och power-heuristik, rysk roulette från studs 3, klamp av indirekt ljus, ray-cone-LOD på skärmen, procedurell natthimmel, dispatcher om 4 spp); `evo_post` (denoiser: demodulering, reprojektion och ackumulering, 3 à-trous-pass, remodulering, primärträffens emission förbi alltihop; ACES-tonemap med statisk dither och CPU-referens); stegplanen (bara de steg en frame visar körs, compositorn får synlighetsbitar) och `HistoryTracker`; Studion (195 instanser av 21 meshar, 29 material, 4 ljus) i FINALE, PORTAL, STUDIO NIGHT, RETURN och REBOOT; DOS-viloskärmen (`C:\DEMO>`) i STUDIO NIGHT och RETURN; `--spp` (fönstret 1, export 64). Specens klart-kriterium för steg 4 ("Studion path-tracad i lavapipe, determinismtest grönt") är uppfyllt: `studio`, `export_mode`, `stream_capture_studio` och `seek_determinism`.

**Beslut i steg 4:**
1. Planens tio beslut accepterade som föreslagna (kontrollerns rulings 2026-10-24): Studion i FINALE, PORTAL, STUDIO NIGHT, RETURN och REBOOT (FLYBY:s avslöjande väntar på steg 5–6); Portal förblir en övertoning till steg 6; CRT:n visar DOS-viloskärmen i de rena 2026-kapitlen; export-`--spp` 64 (tester 2, storyboard 8); studsar 4 i fönstret, 6 i export; à-trous i export bara under 16 spp; fönstret kör den dolda 2026-sidan i BeatSwap/Portal/Wipe (varm historik), exporten hoppar över den; readback per slot och RGB-packning på GPU:n flyttas till steg 8; sync-validering bara i AS-, path tracer- och denoisertesterna; platt CRT-skärm med UV-warp.
2. **Retrobilden** är `R8G8B8A8_SRGB` med `MUTABLE_FORMAT | EXTENDED_USAGE` och formatlistan {SRGB, UNORM}: `view()` är en UNORM-lagringsvy (nivå 0) vars `imageLoad` ger de kodade bytena (palette_map och CRT-passet ser samma bytes som förut); `sampledView()` är en SRGB-vy vars sampel redan är linjärt ljus — avkoda dem aldrig igen. Mips blittas i linjärt ljus. Planens text rättades (184dfec, a386e14). Bilder med två format provas med `vkGetPhysicalDeviceImageFormatProperties2` först.
3. **Rekursionsvakt:** `RayTracingPipeline` söker igenom SPIR-V och vägrar `OpTraceRayKHR` i miss- och closest-hit-shaders och `OpExecuteCallableKHR` överallt (NV-opkoderna kräver NV-tillägg och kontrolleras inte). Valideringslagret fångar inte ett för högt rekursionsdjup, så detta är enda vakten.
4. **Denoiserns varians är kantrobust:** för historik < 4 tas den spatialt som min(Kuwahara, riktad) × 4/len, djup- och normalviktad (planens temporala formel gav 0 vid len 1 och stoppade à-trous helt, även i export); momentbilden lagrar variansen direkt (inte m2), så den inte kollapsar i fp16 över luminans ≈ 245; demodulerat ljus klampas till [0, 60000]; ingen historikklamp (en 3×3 min/max-klamp biasar gles 1 spp mörkt); en firefly klingar av på ~43 frames.
5. **Ljus/instans-validering** i `PathTracer::trace()` före all inspelning: arean = |edgeU × edgeV|, giltig `kind`, en Emitters emission = materialets emission · emissionScale, RetroScreen bara på ett Emissive-material med `kRetroScreenTexture` och emission (s,s,s), ingen instans får namnge en Portal, ingen spegling (det < 0; även `rectLightForQuad`). Ett andra `trace()` eller ett andra TLAS-bygge i samma frame kastar `logic_error`. Inte kontrollerat: att instansens mesh verkligen är ljusets quad, och ljus utan instans (en Emitter utan geometri blir biasad under MIS) — dokumenterat i `scene.hpp`.
6. **Staplade portaler:** för en portal är NEE-pdf:en summan över alla portaler riktningen korsar, samma summa som escape-vikten använder (bias +85 % → 0,1 % i testet). Portalskuggstrålar går till oändligheten; ljusinstanser är ensidiga, andra emitters tvåsidiga och nås bara via BSDF-sampling; riktningar där shading- och geometrinormal är oense släpps av båda strategierna (−0,24 % i ugnen); renderad emission klampas till 65 504 (fp16).
7. **Task 11 slogs ihop före Task 10** med `HistoryState`-kontraktet: renderern räknar fram {reset, framesSinceReset, mode, spp, maxBounces (upplöst, aldrig 0), frameSeed} och `ModernStage` räknar aldrig om seeden (`Stats::frameSeed` visar den, 0 när modernsteget inte körde). Export: `frameSeedFor(t, 0)`; fönstret: `frameSeedFor(t, framesSinceReset)`, så fönstrets första frame efter en återställning är exportens frame.
8. **Studions avvikelser från planen** (godkända): fyra ljus i stället för tre — skärmen 0,3, lampan 0,45, fönstret 0,1 och månen 0,15 (planen: 0,45/0,35/0,20; lampan lyser det mesta kamerorna ser); månskivan borttagen ur himlen, månen är en egen liten Emitter utanför fönstret (fireflies annars); `SceneFrame::clampIndirect` (standard 64) är 2 i Studion; skärmens emissionScale 1,2 (planen 5) så 1993-bilden går att läsa vid rummets exponering; lampan en 10 cm-kvadrat × 35; exponering STUDIO NIGHT +3 EV, REBOOT +1, övriga +2,5; en stol tillagd; CRT-looken tonar scanlines mot medelvärdet 0,875 med avståndet och har vinjett och ett svagt glas-sken. Studions GPU-sida byggs vid första Studio-frame; fönstret anropar `Renderer::prepare()` så kapitel 9 inte hackar.
9. **Strikta Studio-tester:** `export_mode`, `capture_test.py` och `stream_capture_test.py` fallerar om inte alla fem Studio-kapitlen path-tracas (`--show-info` har fått fältet `scene`, "Placeholder" eller "Studio"); `stream_capture` är delat i två CTest-tester (`stream_capture` och `stream_capture_studio`) som körs parallellt; jämförelsen förblir bytexakt.
10. **Material:** single-scatter-GGX (r 0,5 tappar upp till ~16 %; ugnsbandet sänkt 0,85 → 0,83, 2b5808d); dielektriskt F0 från IOR (IOR 1 = ren Lambert, F90 = saturate(50·F0)); ett material är glas när transmission > 0,5; rough glass släpper mikrofacettsampel på fel sida; BSDF:ernas sampling kontrolleras mot en kvadratur av `bsdfEval`. Roterade meshar (lathe, cylinder, sfär) har u åt höger sett utifrån (1efd0f2).
11. **Tonemap:** negativt ljus klampas i AP1 före kurvan (annars blev −1 vitt); platshållaren går genom HDR-bilden via den exakta inversen av tonemappen (`fill_hdr.comp`), så den ser ut som förut (±1).
12. **Seek-determinismen** (Task 12): återställningen är dubbelt säkrad — denoiserns `resetHistory()` och att ingen föregående kamera ges (motion ogiltig, inget reprojiceras). Testet fallerar först när båda tas bort; det är avsiktligt.
13. **Sync-validering i lagret 1.3.275** krediterar inte värdväntningar på timeline-semaforen (`FrameRing::wait`): när VMA återanvänder minnet från en frigjord buffert rapporteras en falsk hazard. Testerna håller buffertar vid liv eller tomgångskör kön mellan deltester. Lagret ser inte heller AS-byggen, trace→trace-hazards eller åtkomst genom deskriptorarrayer, så de barriärerna är handgranskade (RTX-checklistan).
14. **Slutgranskningen (fixvåg F):** path tracern och denoisern delar en bitbaserad ändlighetskontroll (`shaders/common/finite.glsl`: `isFinite`, `isInfinite`, `allFinite`; inga `isnan`/`isinf` kvar, som en fast-math-kompilator kan vika bort) och en färgheader (`shaders/common/color.glsl`: `luminance`, `srgbOetf`, `srgbEotf`); path tracerns utdata är bytelika före och efter på lavapipe. Månens riktning står på ett ställe, `src/rt/sky_constants.hpp`, som både C++ (`rt::kMoonDirection`, som `Studio::kMoonDirection` läser) och `sky.glsl` inkluderar. `evo_modern` länkar inte längre `evo_post`. TIMEOUT 300 för `export_mode`, `seek_determinism`, `studio` och `path_tracer` (`studio` tar ~29 s ensamt och överskred 120 s under last). `FillTarget::Display`/`fill.comp` är bara testreferenser (kommenterat).

**RTX-checklista (ägarens maskin; inget av detta kan provas på lavapipe):**
- Hela `ctest` med ett **nytt valideringslager** (sync-validering slås på i `accel`, `path_tracer`, `denoiser`, `image`): noll meddelanden utöver de två avsiktliga. Gör sedan om barriärmutationerna: ta bort TLAS-barriärerna i `Tlas::build`, barriären mellan trace-dispatcherna (förvänta RAW/WAW på lighting/emission/albedo) och per-nivå-barriären i `generateMips` (35 hazards) — alla ska nu fångas.
- **SBT med paddad layout** (handle 32, alignment 32, base 64; vissa GPU:er base 128): `rt_pipeline`:s prob och path tracern ska ge samma resultat. Task 5:s GPU-prob med överstyrda egenskaper (base 128) är **inte gjord**; tabellen är bara testad på CPU:n.
- **Scratch-alignment 128** (`minAccelerationStructureScratchOffsetAlignment`); konstruktörerna kastar om adressen inte är alignad.
- **Kompaktering** krymper faktiskt (lavapipe: 1480 → 1480 byte); `accel_test` jämför kompakterad mot okompakterad prob med memcmp — kan behöva lättas till 1e-4 där.
- **Facing:** `frontFace`/`gl_HitKindEXT` som på lavapipe (utåtvänd CCW sedd utifrån = front); de ensidiga ljusen och instanser sedda bakifrån.
- **SRGB + STORAGE med EXTENDED_USAGE** för retrobilden (format-probet ska lyckas), och `path_tracer` (h): byte 128 → 0,4331 vid scale 2 genom SRGB-vyn.
- **Ändlighetskontrollen:** path tracern och denoisern använder nu samma bitbaserade `isFinite`/`allFinite` (`shaders/common/finite.glsl`), så fast-math kan inte vika bort dem; stressscenen ska ändå förbli ändlig (powerHeuristic, allFinite) — bekräfta det.
- **Determinism:** samma seed → samma bytes över körningar och nya tracers (BVH-bygge, closest-hit-oavgjorda), `stream_capture_studio`, `export_mode`, `seek_determinism`; tonemappen GPU mot CPU inom ±1 (pow/FMA).
- **Windows TDR vid 4K (mät före långa exporter):** ingen enskild dispatch spårar mer än 4 spp (uppskattat ~0,06–0,15 s i 4K på en 4090), men en exportframe är **en** GPU-submission med ceil(spp/4) dispatcher: i 4K vid 64 spp ~1–4 s, vid 256 spp ~4–16 s (uppskattningar, omätta). Om Windows tål det beror på om drivrutinen preemptar vid dispatchgränser. Kör först `--capture-dir` i 3840×2160 med `--spp 256` i STUDIO NIGHT (t ≈ 205) och ta tid; vid minsta tecken på problem (device lost, fryst skrivbord) delas en frames trace på flera submissions i exportvägen (`runRenderStream`/`runCapture`), begränsade av en strålbudget, t.ex. `samplesPerSubmit = max(1, 4·1920·1080/(w·h))`, i stället för ett fast antal. Det löpande medelvärdet bor redan i bilderna, så dispatchbatch k kan gå i en egen submit; håll uppdelningen en funktion av bara (spp, upplösning) så fönster == export fortfarande gäller.
- `maxShaderGroupStride` (RTX 4096) och pipelineskapande med rekursionsdjup 1.
- `accel_test` skriver fortfarande `warning: synchronization validation unavailable` i stället för `SKIP sync validation` och saknar `SKIP_REGULAR_EXPRESSION` (de andra sync-testerna har dem).
- **`--bench` (steg 5 Task 4; ägarens checkpoint blockerar inte steg 5b):** mäter fönstrets rendering utan fönster. Kör från byggkatalogen (Windows: `Evolution.exe`):
  - `Evolution --bench --width 2560 --height 1440 > bench-1440p.jsonl` — alla kapitel som fönstret renderar dem (spårskala 0,67, 1 spp), 2 uppvärmningsframes och sedan 120 frames från 20 % in i varje kapitel; tar några minuter.
  - `Evolution --bench --width 2560 --height 1440 --trace-scale 1 > bench-1440p-native.jsonl` — samma i full spårupplösning (planens budget gäller skala 1).
  - Snabbtest: `Evolution --bench --chapters 11,19 --bench-frames 60`.
  - Varje rad är ett JSON-objekt per kapitel: `gpuMs` (GPU-tidsstämplar i ms per pass, medel över framesen: `tlas`, `trace`, `denoise`, `upscale`, `tonemap`, `compositor`, `retro`, `fill`, `total`), `width`/`height`, `traceWidth`/`traceHeight`, `traceScale`, `spp`, `device` och `fps` (väggklocka, två frames i flykten). `"timestamps":false` betyder att kön saknar tidsstämplar (tiderna blir 0, `fps` gäller ändå).
  - Jämför med budgeten i steg 5-planen (Global Constraints): hel GPU-frame ≤ 14 ms vid 2560×1440 och skala 1, trace ≤ 9 ms, denoise + upscale + tonemap ≤ 2 ms. Skicka `.jsonl`-filerna till sessionen; avvikelser tas i en senare fixvåg.
  - Titta också i fönstret (`Evolution --width 2560 --height 1440`, eller `--trace-scale 1`): CRT:ns bild ska vara skarp och aktuell (den går aldrig genom uppskalarens historik; vid 0,67 är den något mjukare än vid 1), och detaljer ska skärpas inom ~16 frames efter ett klipp utan spökbilder bakom rörliga kanter.

**Uppskjutet och öppna kvalitetspunkter från steg 4** (ur ledgern):
- **Blotchiga första fönsterframes efter en återställning** vid 1 spp (sök, kapitel, klipp): den kantrobusta spatiala variansen filtrerar glesa mörka sampel för lite — mörka hål i den belysta beigea plasten som försvinner på ~10 frames. Hör till denoisern (Task 8:s val; modellerat RMSE 0,063 mot 0,033 för gles brus).
- **Korn i export ≥ 16 spp** (ingen à-trous enligt Beslut 6): mörka hörn behåller fint korn vid standardvärdet 64; vid 192 spp är stillbilderna rena. Överväg högre spp för slutrenderingen på RTX eller à-trous vid alla spp (ändrar Review Focus 6 vid spp ≥ 16, där fönster och export redan skiljer sig).
- **ACES tvättar ur CRT:ns ljusa mättade färger** mot vitt vid rummets exponering (megamixen förblir läsbar). Grading hör till steg 6.
- **Fasta ljusvalsvikter** (ingen spatial importance sampling): DOS-kapitlen lägger 30 % av samplen på en nästan svart skärm. Viktigt för steg 5:s scener med många ljus.
- **Glaskulans kaustik är svag** (lampan nästan rakt ovanför, clamp 2 dämpar kaustikvägar).
- Single-scatter-GGX förlorar energi i grova metaller; glas tonas av baseColor vid varje genomgång och transmissionen saknar η²-term.
- Fönstret på lavapipe: under 2 fps blir varje frame ett TimeJump, så ackumuleringen byggs aldrig upp där (README säger det). Stats: `frameSeed` 0 betyder "inte körd", fast en riktig seed kan vara 0.
- Småfynd: `HistoryState` bär numera inställningar och seed, inte bara historik (namnet); `frameSeedFor` bor i det tunga `path_tracer.hpp` (en lätt header vore bättre); `stream_capture_test.py` har kvar Studio-titlarna i `STUDIO_CHAPTERS` vid sidan av `scene`-fältet; compositorns CRT-på-pixelkontroll och en `SKIP_AABBS`-flagga på strålarna (prestanda) gjordes inte; `Image` exponerar inte usage, så denoisern kan inte kontrollera STORAGE på sina indata; `lathe` kastar på upprepade profilpunkter (inga skarpa veck); en firefly kan spridas av à-trous innan remodulering (ändlig, klampad).
- `Renderer::resize` väntar med `waitAll` (timeline-värdväntan): med sync-validering påslagen i fönstret kan en storleksändring ge lagrets falska hazard (se beslut 13).
- Steg 2:s rekommendation om readback per slot och RGB-packning på GPU:n är flyttad till steg 8 (Beslut 8).
- Kvar från Task 10:s granskning (inte i fixrunda E): `shotFor` bygger en hel SceneFrame (195 instanser) bara för att läsa shotten, två gånger per frame; lysdioderna är emitters utan ljus (fireflies); den dubbla återställningsvakten (Beslut 12). Från slutgranskningen: `writeGuides` spårar en extra ojittrad primärstråle per frame (~+10 % strålar vid 1 spp; återanvänd sampel 0:s primärträff vid aperture 0 i 4K60); `tonemap_reference.cpp` (evo_post) inkluderar `rt/random.hpp` utan att bero på `evo_rt` (header-only, fungerar; kan flyttas till ett neutralt ställe).

**Storyboard vid steg 4:s slut** (`--spp 8`, 1 min 49 s): 1993-cellerna ser ut som i steg 3; Studio-cellerna i FINALE, PORTAL, STUDIO NIGHT, RETURN och REBOOT ser rätt ut (beige CRT med live-bild, lampan, fönstret mot staden, krom- och glaskulan, DOS-listningen läsbar på skärmen i STUDIO NIGHT och RETURN). Synligt men inte trasigt: korn på den beigea plasten i närbilderna (FINALE/PORTAL 20 %, REBOOT) vid 8 spp, megamixens färger tvättade mot pastell av ACES, lampskärmens topp klipper vitt i STUDIO NIGHT:s orbit, och vidbilderna vid 80 % är mycket mörka utanför skrivbordet.

**Steg 5 Task 14, Studio-polish (2026-10-25):** ett modernt rum (Beslut 13: polerat betonggolv i plattor och en betongvägg med det nya mönstret `Pattern::Concrete` = 3, putsade väggar, ekskrivbord, smal svart fönsterram, en skyline av mörka torn med tända fönster bortom fönstret) där 1993-datorn är det enda 1993-föremålet; 480 instanser, 33 material. Ljusrigg per kapitel (`StudioRig studioRig(Effect)`, i framens materialtabell): STUDIO NIGHT lampan av och skärmen 8 (planen 3,5: DOS-skärmen är nästan svart, och dess grå text tål förstärkningen), FINALE/PORTAL lampan 0,2 och skärmen 2,4 (planen 3,0) vid +0,2 EV så att megamixens färger håller sig mättade under PBR Neutral (visningsförstärkning ≤ ~2,4, kontrollerns ruling om pastell-CRT), RETURN/REBOOT lampan 1 och skärmen 1,5. Lampans diffusor är en 8 cm-kvadrat infälld 3,5 cm i en matt grå skärm; lysdioderna och skylinens fönster är `kMaterialCameraOnlyEmission` (emissive.rchit nollar dem efter en grov vertex, coneSpread < 0, tills Task 6 gör det i raygen). Månen har en egen riktning (44°, `Studio::kMoonDirection`, himlen `studioEnvironment()` följer den) så månstrålen faller förbi skrivbordet på golvet, intensitet 700 (mer gav studsbrus i vidbilderna); horisonten ljusare som sval fyllnad. Kromkulan står framför skärmens högra sida, glaskulan mellan lampan och skärmen. Vidbilderna: ögat 1,1 m, 32°, rummet djupare (framväggen z = 4), stolen i månstrålen som förgrundssiluett. CRT:n: krökt glas för reflexer (skuggningsnormalen, quaden platt), grågrönt glas, strålprofil över 200 linjer med medel 0,875 som tonar över LOD −0,5…0,5, aperture grille under LOD −2. `clampIndirect` 0,5 i Studion (2 gav gnistor på lampbelysta ytor vid export-spp; 16 först med Task 6). Testerna som ändrades medvetet: `studio_test` (b) i STUDIO NIGHT med ≥ 40 % och väggen < 0,002 linjärt, (i) klamp 0,5, nya (j)–(n); `environment_test` (0) Studions egen himmel och (e) nya mätvärden; `light_selection_test` (d) i RETURN med < 8 %; `scene_table_test` 480 instanser.

## Nästa steg: steg 5

Specens §7, steg 5: **moderna scener** — partiklar (compute), nebulosa (volym), tunnel, glas-glenz, lins/schack, kulor, stad och skepp; klart när alla 2026-rutor finns (de 23 kapitel vars 2026-sida i dag är platshållargradienten: akt I:s åtta effektkapitel från FLYBY till VECTOR CITY och akt II:s och III:s 15 planetkapitel; FLYBY:s Studio-avslöjande kommer med skeppet). Planen skrivs först och godkänns av ägaren. Rekommendationer ur steg 4 (helgrenens slutgranskning; uppskattningarna är omätta):

**Första tasken: en delad tracer och en delad denoiser.** Ge **inte** varje scen en egen `Traced`. I dag äger en `Traced` en hel `PathTracer` (pipeline + SBT, TLAS + scratch, buffertar per slot och en egen G-buffer, 52 B/px) och en egen `Denoiser` (8 RGBA16F-bilder, 64 B/px): ~0,96 GB per scen i 4K, med ~9 path-tracade scener ~8,6 GB render targets där bara en används per frame, och samma RT-pipeline kompilerad om per scen (`prepare()` bygger allt i förväg: antingen växer starten linjärt eller hackar kapitelstarterna). Dela i stället `PathTracer` i två:
- **Delat:** en tracer (pipeline/SBT, TLAS, uniform-, instans- och ljusbuffertar per slot, svart textur, G-buffern) och **en** `Denoiser`. Denoiserns historik återställs vid varje kapitelbyte (`HistoryTracker` gör det redan).
- **Per scen (`SceneGpu`):** `GeometryStore`, `BlasSet`, materialbuffert och ljus — små, byggda för varje scen i `prepare()`. `trace(cmd, SceneGpu&, frame, …)` binder scenens buffertar.
- **En scentabell** i stället för `SceneId`-switchen i `sceneFor`/`makeScene`/`ModernStage`, så att en ny scen är en rad. Minnet stannar då på ~1,2 GB i 4K oavsett antal scener.
- Om steg 6 någonsin behöver två path-tracade scener i samma frame (t.ex. en portal Studio → skepp) behövs en kombinerad TLAS eller två G-buffrar, eftersom bara ett `trace()` per frame tillåts; bestäm det i steg 5:s plan.

**Övriga punkter för steg 5:s plan (i ungefärlig ordning):**
1. **Miljö per scen och himmelssampling.** `skyRadiance` är en hårdkodad natthimmel (med Studions stad under horisonten) som alla scener ser. Utan portal är `portalPdf` 0 och himlen nås bara via BSDF-sampling: brus från varje liten ljus himmelsdetalj i utomhusscenerna (SPHERE STORM, CITY NIGHT, METROPOLIS, PLASMA PLANET …). Gör miljön till en del av scenen (ett `EnvironmentId` + parametrar i `FrameUniforms`, eller en himmelsvariant per missindex) med en samplingsstrategi: en importance-samplad analytisk sol-/månskiva och/eller en "kupolportal" (en stor virtuell portal eller sfärljus) så NEE och MIS täcker himlen. Sol-/månriktningen som uniform (i dag konstanten `rt::kMoonDirection` i `src/rt/sky_constants.hpp`).
2. **Spårupplösning skild från utupplösningen, med uppskalning.** `ModernStage` spårar i dag i utstorleken. Nativ 4K vid 1 spp, 4 studsar är ~17–32 ms per frame på en 4090 (~30–60 fps, marginellt; ~25–45 ms på en 4080): trace ~12–25 ms, denoiser ~4–5 ms, tonemap ~0,3 ms, compositor/HUD/blit ~0,5–1 ms. I 1440p internt (3,7 Mpx) är trace ~6–15 ms → 60 fps. Lägg till en temporal uppskalning (TAAU/FSR2-aktig) efter denoisern, eller bilinjär plus CRT:n; den delade tracern ovan är stället där spårstorleken blir en egen parameter.
3. **Dynamisk geometri.** `BlasSet` byggs en gång, kompakterad och opak. Partiklar (2M i korten) inte som TLAS-instanser: antingen ett compute-pass som skriver en vertexbuffert följt av BLAS-*build/refit* per frame (`ALLOW_UPDATE`, BLAS per slot eller dubbelbuffrad, barriär COMPUTE-skrivning → AS-bygge), eller procedurella AABB:er med en intersection-shader (en ny hit-grupptyp; pipelinen har i dag 4 triangel-hitgrupper och SBT:n antar dem). Billboards/splats sammansatta mot `depth`-guiden (linjärt vy-djup, −1 vid miss) är det billiga alternativet för subpixelpartiklar; de ska komma in före denoisern eller som egen emission.
4. **Fler instanser.** `PathTracer::kMaxInstances = 1024` (TLAS): höj och gör det till en kapacitet per scen (TLAS-kostnaden är linjär och liten, ~0,1 ms per 10k instanser på RTX) — kulor som instansierade UV-sfärer, staden som instansiering i stor skala.
5. **Volymer/medium (nebulosan).** Finns inte alls: en mediummodell (ray-marchad eller delta-trackad densitet i raygen mellan ytträffar, rekursionsdjup 1 kvar), fasfunktion, NEE genom mediet och ett beslut om denoising (volymljus är lågfrekvent: en separat, billigare och hårdare filtrerad buffert fungerar bra). Glas-glenz/linsens dispersion kräver spektral eller per-kanal-IOR, som BSDF:en saknar.
6. **Rörelsevektorer per objekt.** I dag rör sig bara kameran (`previousCamera`). Rörliga instanser (skepp, kulor, partiklar) behöver föregående transform per instans (`InstanceRecord.prevTransform`), annars ghostar eller förkastar denoisern dem.
7. **Material- och ljusvarianter per frame (ljusrigg per kapitel).** `SceneStatic::materials` laddas upp en gång och valideringen knyter emission till materialet. En ljusrigg per kapitel (CRT:n som nyckelljus i STUDIO NIGHT), pulserande emitters på beatet eller en stad med blinkande fönster kräver materialvarianter som byts per instans (billigt, fungerar i dag) eller en liten överstyrningsbuffert per frame. Välj en i planen.
8. **Ljusval per punkt.** Ljusvalet är en linjär CDF med fasta vikter (`lights.glsl`, `kMaxLights = 64`). Minst: vikter per punkt (effekt·cos/d², skärmens effekt ur retrobildens 1×1-mip) via samma funktion i `pickLight` och `lightSelectPdf`. För CITY NIGHT och METROPOLIS: ett ljus-BVH eller ReSTIR DI över emitterande trianglar.
9. **Blotchiga första frames och exportkornet** (ovan) åtgärdas innan scenerna blir många — varje 2026-kapitel ärver dem.
10. **Testbudget per scen.** En liten frame per shot i `stream_capture_studio` (via `scene`-fältet) och i `seek_determinism` per scen; TIMEOUT 300 finns, men överväg CTest `RESOURCE_LOCK`/`PROCESSORS` för de lavapipe-tunga testerna så de inte alla överlappar.

Behåll reglerna: ljusen härleds ur sina material (valideringen kastar annars), inga koplanära emitters och väggar (closest-hit-oavgjorda är en determinismrisk på RTX), inget glas framför en texturerad emitter eller i en portal, samma UV-warp på träff- och NEE-sidan.

**Prestandautsikt för slutrenderingen (uppskattningar för RTX 4080/4090, omätta).** Antaget 3–6 G strålar/s effektivt för en inkoherent megakernel i ~100k trianglar, ~14 strålar per väg vid 6 studsar; 4K = 8,29 Mpx.
- **Export i 4K vid 64 spp, 6 studsar:** ~4–7 G strålar per frame, alltså ~0,7–1,5 s/frame på en 4090 och ~1,2–2,5 s på en 4080; denoiser, tonemap och readback (~33 MB, RGB-packning ~10–20 ms) är försumbara bredvid.
- **Hela showen:** Studio-framesen i dag (~86,7 s, ~5 200 frames) ~1–3,5 h; efter steg 5 ~30–35k path-tracade frames, alltså **~6–24 h vid 64 spp** och 1–4 dygn vid 256 spp. Kostnaden domineras av spp, så den stora hävstången är att minska bruset vid källorna (ljusval per punkt, vägregularisering, klamp först efter en grov vertex) plus ett milt exportfilter vid ≥ 16 spp, så att 64 spp ser ut som dagens 192–256.
- `ring.wait(ring.submit())` per frame serialiserar CPU och GPU: < 2 % på path-tracade frames men ~20–30 fps-tak för 1993- och platshållarkapitlen i 4K (steg 8:s readback per slot).

**Minne i 4K för en path-tracad scen (~1,25 GB):**

| Post | Storlek |
|---|---|
| G-buffer | 431 MB |
| Denoiser | 531 MB |
| HDR + LDR (modernsteget) | 100 MB |
| Compositorns skärm + utbild | 66 MB |
| Readback | 33 MB |
| Swapchain | ~100 MB |
| BLAS/TLAS och geometri | några MB |

Ryms lätt i 12–24 GB. Med en `Traced` per scen skulle det växa med ~1 GB per scen (därav första tasken); denoiserns `guides_`-par (2 × 66 MB) duplicerar tracerns normal och djup och kan tas bort senare.

**Slutleverans** (oförändrad, se ovan): när showen är klar renderas den i **4K60** på ägarens RTX-maskin med exportkedjan (`--render-stream` vid önskat `--spp` till ffmpeg; `tools/render_video.py` finns inte än och hör till steg 8; lavapipe räcker bara till stillbilder och korta klipp), ägaren laddar upp filen till sessionen, och sessionen publicerar den **privat på YouTube** via vidIQ (`vidiq_video_upload`, privacy private). Kortare förhandsvisningar i låg upplösning kan renderas i containern.

**Worktrees.** Huvudträdet `/home/user/PulseAscendant` står på `claude/evolution-showcase-plan`; parallella implementers jobbar i `/home/user/wt-a`, `/home/user/wt-b` och `/home/user/wt-c` (grenarna `step4-a`, `step4-b`, `step4-c` i steg 4; `step3-a`/`step3-b` i steg 3), som spolas fram till huvudgrenen efter varje sammanslagning (`git worktree add /home/user/wt-a -b step4-a`). En implementer jobbar bara i sin egen worktree, och en reviewer läser bara (bygger inte). En worktree konfigureras på sekunder utan nedladdning genom att peka FetchContent på huvudträdets källor och länka in musiken:

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

Fullständiga loggar finns i `.superpowers/sdd/2026-10-25-step3-retro-effects/progress.md` och `.superpowers/sdd/2026-10-26-step4-rt-foundation/progress.md` (rapporterna `task-*-report.md`, `fixround-*-report.md` och granskningarna bredvid), men de är git-ignorerade och följer inte med till en ny session; allt som behövs står ovan.
