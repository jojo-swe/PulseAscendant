# Handoff — EVOLUTION (uppdaterad 2026-10-24, steg 2 klart och slutgranskat)

Läs detta först i nästa session. Specen och planerna ligger bredvid.

## Läge

- **Spec:** `2026-10-21-evolution-design.md` — godkänd av ägaren. Alla standardval i avsnitt 9 gäller (Vulkan 1.3 + RT krävs, Studion, DOS-boot, två epoker, CRT på, engelsk text, syntetisk placeholder-musik). Credits-texten är inte bekräftad. CREDITS (kapitel 29) är sedan steg 2 ett 1993-kapitel: en sinusscroller med `eraFixed` 0 (se steg 2 nedan).
- **Planer:** `2026-10-21-step0-skeleton.md` (steg 0, klart), `2026-10-23-step1-audio-timeline.md` (steg 1, klart; amendment med tre akter), `2026-10-24-step2-retro-pipeline.md` (steg 2, klart; slutgranskat med en fixvåg). Status per task står längre ner här.
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

Beslut i steg 2 Task 7 (storyboard): `tools/storyboard.py --app EXE --out DIR [--width 640 --height 360] [--chapters 0,3] [--no-crt]` fångar varje kapitel vid 20 %, accenten och 80 %, en gång tvingat till 1993 och en gång till 2026, och sätter ihop en PNG per akt (`act1.png`–`act3.png`; rader = kapitel, sex kolumner, cellerna nedskalade 2× med boxfilter, 2 px mörkgrå gutter, etiketter i ett eget 5×7-typsnitt eftersom HUD-titeln blir oläslig i halv storlek; HUD:en är ändå på). Tvingandet är den diagnostiska miljövariabeln `EVO_FORCE_ERA` (`0`/`1`, annat ignoreras) som bara capture-läget läser: den sätter `era`, `wipe` (0 eller 1) och `eraCut` 0. Hela showen i 640×360 tar ~25 s på lavapipe. CTest `storyboard` kör kapitel 0 och 3 i 320×180 och kontrollerar storlek och att 1993- och 2026-cellerna skiljer sig; `capture` kontrollerar att `EVO_FORCE_ERA=1` ändrar BOOT och att ogiltiga värden ignoreras.

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

1. Installera miljön (ovan), bygg och kör `ctest` i `evolution/` — allt ska vara grönt (23 tester). Lägg MP3:orna i `music/source/` och kör `prepare_show.py` (se Musik ovan) om du vill höra showen; konfigurera om efteråt så att `show.wav` länkas in.
2. Steg 2 är slutgranskat och fixat (se ovan). Låt ägaren köra Windows-kontrollen, titta på storyboarden (`python3 tools/storyboard.py --app build/linux/bin/Evolution --out build/linux/build-out/storyboard`) och skriv sedan steg 3:s plan med rekommendationerna ovan (resterande retroeffekter) och kör den med `bash <superpowers>/skills/subagent-driven-development/scripts/task-brief <plan> <n>`, implementer (Opus) + reviewer per task. Ledgern i `.superpowers/` är git-ignorerad och försvinner med containern; den här filen är ledgerns ersättning — uppdatera den vid sessionsslut.
3. När steg 3 är klart: slutgranskning av steg 3, sedan nästa stegs plan.
