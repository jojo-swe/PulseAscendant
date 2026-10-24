# EVOLUTION — designspec

Ett realtidsdemo som sätter **1993** och **2026** i samma bildruta. Ny kodbas,
Vulkan-first, hårdvaru-raytracing som krav. Musik: nya Suno-låtar av joyolo.
Status: **godkänd 2026-10-21**. Implementationsplaner per steg läggs i `evolution/docs/plans/`.

Demot bor som ett **fristående projekt i katalogen `evolution/`** i
PulseAscendant-repot: egen CMake, egna källor, shaders, verktyg och tester.
Det delar ingen kod med den gamla motorn och rör inte dess bygge. Det här
dokumentet ersätter den första planen som byggde vidare på den motorn.
Dramaturgin (avsnitt 2) och låtönskemålen (avsnitt 5) är desamma; tekniken
(avsnitt 3–4, 6–7) är ny.

## 1. Mål och avgränsning

**Mål.** Återskapa känslan av att tappa hakan framför Second Reality 1993,
men med avståndet mellan då och nu som själva poängen. Varje effekt visas
först som den såg ut då (320×200, 256 färger, 486-känsla) och sedan som
modern hårdvara kan göra den i realtid (4K, HDR, path tracing, miljontals
partiklar). Musiken går samma resa, från tracker till modern produktion.

**Avgränsning.**
- Riktar sig till en GPU med Vulkan 1.3 och ray tracing (RTX 20-serien eller
  nyare, RX 6000 eller nyare). Saknas det visar demot ett tydligt
  meddelande och avslutar. Alla andra får filmen.
- Windows och Linux. Ingen OpenGL, ingen raster-fallback.
- Vi återskapar *effektprinciper*, inte Second Realitys grafik, kod, logotyp
  eller musik. Enda undantaget är glenz-geometrin (14 vertex / 24 trianglar,
  Unlicense, Future Crew krediteras), som flyttas över från Pulse Ascendant.

## 2. Dramaturgi

### 2.1 Ramberättelse: Studion

Demot öppnar och stänger i **Studion**: ett modernt, path-tracat rum där en
1993-PC med CRT-monitor står på ett skrivbord. På skärmen kör retrodemot.
Kameran flyger *in i* skärmen och 1993 fyller bilden. När musiken släpper
dras kameran *ut* igen, vänder sig om, och samma effekt står i rummet (eller
har blivit en hel värld) i 2026-version. Skärmen är portalen mellan
epokerna, och Studion är den plats där raytracingen får visa vad den kan:
glas, metall, mjuka skuggor, indirekt ljus från en CRT som lyser upp ett
mörkt rum.

### 2.2 Tre övergångstyper

| Typ | Vad som händer | Används när |
|---|---|---|
| **Portal** | Kameran flyger in i/ut ur CRT-skärmen i Studion | Första och sista kapitlen, plus 2–3 nyckelkapitel |
| **Beat-swap** | Hård växling då↔nu på taktslag: var 4:e takt, sedan var 2:a, sedan varje slag, tills "nu" vinner | Högenergikapitel, in i drop |
| **Wipe** | Vertikal skarv som svepar i takt med taktfasen; delad bild "1993 \| 2026" | Kapitel där man ska *se* skillnaden sida vid sida |

Växlingspunkten ligger på kapitlets **accent** (det starkaste uppmätta
anslaget 28–76 % in i kapitlet), så flippen landar på musiken utan
handjustering.

### 2.3 Kapitelplan (följer låten; tider sätts av musikregin)

| # | Kapitel | 1993 (320×200, 256 färger) | 2026 | Övergång |
|---|---|---|---|---|
| 0 | **BOOT** | DOS-prompt skrivs: `C:\DEMO>EVOLUTION.EXE`, ljudkortsdetektering, "LOADING…" | — | Klipp |
| 1 | **FLYBY** | Flat-shadeat rymdskepp mot stjärnfält, sinus-scroller | Samma skepp path-tracat med metall och glas, volymetriska motorspår, stjärnor med flares | Portal (första avslöjandet av Studion) |
| 2 | **DOTS** | Några hundra punkter morfar kub → sfär | 2–4 miljoner partiklar i compute morfar kub → sfär → torusknut → logotyp; DOF, motion blur, basdriven turbulens | Beat-swap |
| 3 | **PLASMA** | Palettcyklande sinusplasma | Volymetrisk nebulosa: raymarchad densitet med emission, ljusspridning | Wipe |
| 4 | **TUNNEL** | Ringtunnel av punkter med vobblande centrum | Oändlig tunnel med PBR-paneler, path-tracade reflektioner, ljusstrålar i dimma | Portal |
| 5 | **GLENZ** | Transparenta flat-shadeade polygoner (den krediterade meshen) | Riktigt glas: refraktion, dispersion och kaustik via ray tracing | Beat-swap |
| 6 | **ROTOZOOM / LENS** | Roterande, zoomande schackmönster; "lins" som förstorar en bild | Oändligt schackgolv med speglingar, glassfär som rullar över det | Wipe |
| 7 | **RAYTRACE** (klimax) | "Raytracade" kromkulor på schackbräde i ryckiga 8–12 fps, hyllning till 1993 års *förberäknade* animationer | Äkta path tracing i realtid: dussintals krom- och glaskulor, mjuka områdesljus, flerstudsreflektioner, 60 fps | Beat-swap in i låtens drop |
| 8 | **VECTOR CITY** | Flat/gouraud-stad med ~200 polygoner | Neonstad med miljontals fönster, volymetrisk dimma, blöta gator med speglingar | Wipe → Portal |
| 9 | **FINALE** | Delad bild: alla effekter "då" i ett rutnät | Kameran drar sig ut ur CRT:n; monitorn spelar 1993 medan rummet exploderar i 2026-partiklar | Portal |
| 10 | **CREDITS** | Sinus-scroller med greetings | Modern typografi ovanpå; "THE END?" → "REALITY IS WHAT YOU RENDER" | — |

Varje kapitel bär en **epok-skylt** med *beräknade* siffror, t.ex.
`1993 · 320×200 · 256 COLORS · 8 FPS · 512 POINTS` mot
`2026 · 3840×2160 · HDR · PATH TRACED · 60 FPS · 2,097,152 POINTS`.
Siffrorna hämtas från faktisk konfiguration.

Flera låtar hanteras som i Pulse Ascendant: spåren läggs efter varandra
utan tystnad, och varje spår bär sin egen kapitelföljd.

### 2.4 Tre akter (beslutat 2026-10-24, tre låtar av joyolo)

| Akt | Låt | Längd | Tema | Kapitel |
|---|---|---|---|---|
| I · DÅ/NU | Amiga Trance Demo (~143 BPM) | 3:14 | Då/nu-paren ovan (kapitel 0–10). Slutar med PORTAL: kameran dras ut ur CRT:n in i Studion. | BOOT, FLYBY, DOTS, PLASMA, TUNNEL, GLENZ, LENS, RAYTRACE, VECTOR CITY, FINALE, PORTAL |
| II · NU | Guitar Trance (~140 BPM) | 3:38 | Enbart 2026. Resan ut ur Studion och tillbaka: staden i natt, nebulosa, glaskatedral, partikelhav, sfärstorm, singularitet. | STUDIO NIGHT, ASCENT, CITY NIGHT, NEBULA, GLASS CATHEDRAL, PARTICLE OCEAN, SPHERE STORM, SINGULARITY, RETURN |
| III · BORTOM | Rigid Arpeggios (~138 BPM) | 4:59 | CRT:n i Studion bootar 1993 igen, kameran går *genom glaset*, och varje 1993-effekt återkommer en tredje gång i planetär skala: "tänk om 1993 hade haft vår hårdvara". Slutar med credits och 1993 års THE END-skärm. | REBOOT, GALAXY, PLASMA PLANET, WORMHOLE, CRYSTAL CITY, LENS OF GOD, RAYTRACE INFINITY, METROPOLIS, ASCENDANT, CREDITS, THE END |

Totalt 31 kapitel, 711,16 s (11:51). Akt II och III är 2026-only (`Transition::Cut`
med fast era 1), utom REBOOT (portal 1993→2026) och THE END (1993).
Akt III:s världar byggs som varianter av akt II:s scener i större skala, så
kostnaden är parametrar och kameror snarare än nya renderare.

**Tempo.** De tre låtarna är trance kring 140 BPM med punkterade
åttondels-delay; den ärvda beat-trackern låste på 2/3-tempot. Pipelinen
får därför ett disambigueringssteg som väljer det tempo där baskaggen
träffar bäst (avsnitt 3.7).

## 3. Teknisk arkitektur

### 3.1 Stack

| Del | Val | Varför |
|---|---|---|
| Språk/bygge | C++20, CMake 3.25+, presets för Windows (MSVC) och Linux (GCC/Clang) | Samma arbetssätt som tidigare, bevisat på båda plattformar |
| Fönster, input, ljud | SDL3 (pinnad commit med SHA-256, statiskt länkad) | Känt, litet, fungerar headless med `dummy`-drivrutiner |
| Grafik | **Vulkan 1.3**, inget annat API | Dynamic rendering, synchronization2, timeline semaphores, buffer device address, descriptor indexing |
| Ray tracing | `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`, `VK_KHR_ray_query` | RT-pipeline för path tracing; ray queries i compute för skuggor/AO i partikel- och volympass |
| Laddare | volk (MIT) | Inga krav på Vulkan SDK vid körning |
| Minne | Vulkan Memory Allocator (MIT) | Standard, sparar veckor |
| Shaders | GLSL → SPIR-V vid bygge med `glslangValidator`; `.spv` följer med i release-paket | Inga runtime-kompilatorer |
| Bild-I/O | stb_image_write (PNG-captures) | Minimalt |
| Musikverktyg | Python 3 + numpy + ffmpeg, portade från Pulse Ascendant | Redan verifierade |

Alla beroenden hämtas med FetchContent mot pinnade commits/hashar; inget
systemberoende utöver kompilator, CMake, glslangValidator, Python och ffmpeg.
Bygget körs från `evolution/` (`cmake --preset linux` eller `windows`),
helt oberoende av repots toppnivå-CMake.

### 3.2 Layout (allt under `evolution/`)

```
evolution/
CMakeLists.txt  CMakePresets.json  README.md
src/
  main.cpp
  app/      fönster, loop, input, CLI-flaggor, headless-läge
  gfx/      instans/enhet/kö, swapchain, allokering, pipelines, descriptors, shaders, frame-graf
  rt/       BLAS/TLAS-byggare, instanser, materialtabell
  audio/    SDL3-uppspelning, sampleexakt klocka, seek
  show/     timeline, cues, era/transition, beat-grid (genererade data-headers)
  retro/    320×200 indexerad pipeline: effekter, paletter, CRT
  modern/   scener: studion, partiklar, nebulosa, tunnel, glas, lins, kulor, stad
  post/     ackumulering/denoise, bloom, grading, compositor, text
shaders/    GLSL (comp, rgen, rchit, rmiss, vert, frag) + common/
tools/      prepare_show.py, analyze_music.py, analyze_reactivity.py, beat_grid.py, direct_music.py, render_video.py, …
tests/      CTest: enhetstester + headless renderingstester
music/      receipts, analysis, direction (JSON)
assets/     musik (ignorerad i git), font-atlas, paletter
docs/       plans/, verifiering, källkarta
```

### 3.3 Frame-graf (en bildruta)

```
retro:   effect.comp (320×200 R8 index, låst till retro-fps, cachad)
         → palette.comp (RGBA8, palettcykling)
         → crt.comp (4:3, scanlines, mask, krökning)        → retroImage
modern:  tlas-build (animerade instanser)
         → pathtrace.rgen/rchit/rmiss (1 spp interaktivt, N spp vid export)
         → particles.comp (position/färg = f(id, t)) → particles.vert/frag (additiv, in i HDR)
         → volume.comp (nebulosa/dimma, ray queries för skuggning)
         → accumulate.comp (temporal reprojektion, varians) → atrous.comp ×3
         → bloom.comp (pyramid) → grade.comp                  → modernImage
compose: composite.comp(retroImage, modernImage, era, transition) + text.comp → swapchain/export
```

Passen är explicita funktioner över en enkel frame-graf som sköter
barriärer och bildlayouter. Inget pass körs om kapitlet inte behöver det
(BOOT kör bara retro + compose).

### 3.4 Path tracing

- RT-pipeline med en raygen, en closest-hit per materialklass (opak PBR,
  glas, emissiv, procedurellt schack) och en miss (himmel/rum).
- Interaktivt: 1 spp, upp till 4 studsar, next-event estimation mot
  områdesljus, temporal ackumulering med reprojektion och à-trous-filter.
  Ingen ML-denoiser i v1.
- Export: valfritt antal spp per bildruta (t.ex. 64–256 vid 4K60) utan
  temporal filtrering. Filmen blir därför renare än den interaktiva
  körningen, och det sägs rakt ut i README.
- Alla "nu"-scener är RT-scener. Volymer (nebulosa, dimma) raymarchas i
  compute och skuggas med ray queries mot samma TLAS.

### 3.5 Partiklar

Compute-shader, 2–4 miljoner, **tillståndslösa**: position och färg är
funktioner av id och tid med curl-brus och analytiska morph-mål. Ingen
simulering att spara, exakt samma bild vid seek och export. Ritas som
additiva punkter/quads med dynamic rendering in i HDR-bilden före
ackumuleringen; djuptestas mot path tracerns primärdjup.

### 3.6 Retro-pipeline

1. Effekter renderar **palettindex** (R8) vid 320×200 i compute. Varje effekt
   definierar sin palett (gradienter; 16 nyanser × 16 färger för 3D).
2. Palettuppslag + cykling via offset per intervall.
3. Bayer-dithering (2×2/4×4) där effekten räknar kontinuerliga värden.
4. Bildfrekvenslås: `floor(t·rate)/rate`, 35 Hz standard, 8–12 fps i
   RAYTRACE-kapitlet. Bilden cachas mellan uppdateringar.
5. 4:3 med pixelaspekt 1,2, nearest-neighbour, centrerat i 16:9.
6. CRT-pass (dämpat, togglas med **C**): scanlines, lätt krökning,
   fosformask, bloom, vinjett.
7. Egen 8×8 bitmappsfont för DOS-prompt och scrollrar; egen 5×7 för HUD.

Retro-bilden finns också som textur i Studion: CRT-skärmen i den
path-tracade scenen samplar den, med emissivt material så att den lyser upp
rummet.

### 3.7 Timeline, musik och determinism

- **Sampleexakt ljudklocka** (48 kHz stereo PCM16 WAV, spår efter spår utan
  tystnad). Animation läser ljudpositionen; mute stoppar inte klockan.
- Musikdata genereras av de portade verktygen: 24 FFT-band vid 20 Hz,
  100 Hz transienter i bas/mellan/diskant, beat-grid med takter, och
  kapitelstarter valda av `direct_music.py` på uppmätta anslag.
- `TimelineFrame` bär kapitel, effekt, `era` (0 = 1993, 1 = 2026,
  mellanvärden under wipe/portal), övergångstyp, `eraCut`, accent, beat- och
  taktfas, shot-index, spektrum och transienter. Allt räknas ut
  deterministiskt ur tid, så seek och export ger identiska bilder.
- Partiklar är tillståndslösa (3.5); path tracern är deterministisk per
  bildruta vid export (fast seed per frame); temporal ackumulering används
  bara interaktivt.

### 3.8 Headless och export

Rendering kräver inget fönster: `--render-stream --fps 60 --width 3840
--height 2160 --spp 64` skriver RGB-bildrutor till stdout och
`tools/render_video.py` kodar med ffmpeg (NVENC eller x264) och verifierar
bildantal, längd och ljudlängd. Samma väg används av testerna.

### 3.9 CLI och tangenter

- `--show-info` (JSON med spår, kapitel, starter), `--time`, `--duration`,
  `--width/--height`, `--fps`, `--spp`, `--render-stream`, `--capture-dir`,
  `--no-audio`, `--retro-only`, `--modern-only`, `--no-crt`, `--pulse-gain`.
- Tangenter: **←/→** kapitel, **1–9, 0** direkt till kapitel, **Tab**
  tvinga då/nu, **C** CRT, **M** mute, **F11** fullskärm, **Esc** avsluta.

## 4. Vad som flyttas över från Pulse Ascendant

Allt är din egen MIT-kod eller public domain med kreditering, och **kopieras**
in under `evolution/` så att demot är fristående (ursprunget noteras i
filhuvudet):

- `tools/analyze_music.py`, `analyze_reactivity.py`, `beat_grid.py`,
  `direct_music.py`, `prepare_show.py` (generaliserad till N spår),
  `render_video.py`, `render_chunked.py`, `verify_video_audio.py`.
- Ljudklockans princip och SDL-kökompensation (skrivs om mot SDL3-strömmar i
  den nya strukturen).
- Glenz-meshen och krediteringstexten för Future Crew.
- Mallen för `licenses/MUSIC-NOTICE.txt` och receipts i `music/`.

Inget av renderingskoden flyttas; den är OpenGL-bunden.

## 5. Önskemål på låten/låtarna (Suno)

- **Längd:** 4:00–5:00 per låt. Två låtar ger ~9 minuter, vilket är övre gräns.
- **Tempo:** konstant 4/4, gärna 128–140 BPM. Beat-trackern följer drift men
  inte halvtempo-byten.
- **Struktur som berättar evolutionen:**
  - 0:00–0:30 — chiptune/tracker-intro (FastTracker-känsla, kvadratvågsleads,
    8-bitars trummor), tydlig upptakt.
  - 0:30–2:00 — 90-tals demoscen-trance/techno med tydliga takter.
  - 2:00–2:45 — bryggan: modern produktion tar över.
  - ~2:45–3:00 — **uppbyggnad och tydligt drop** (RAYTRACE landar här).
  - 3:00–4:15 — stort, modernt, filmiskt.
  - 4:15–slut — utro som återbesöker intro-temat.
- Tydliga anslag på downbeats; undvik lång ambient utan puls.
- **Leverans:** WAV eller högsta MP3-kvalitet + Suno-ID för krediterna.

Promptförslag:
> "Evolving instrumental: starts as 1993 PC tracker chiptune (square leads,
> 8-bit drums, FastTracker vibe), grows into 90s demoscene trance, then
> transforms into modern cinematic EDM with huge drop at 2:50, constant 134 BPM,
> 4/4, epic outro reprising the chiptune theme. No vocals."

## 6. Verifiering

Den här utvecklingsmiljön har **Mesa lavapipe med Vulkan 1.4, ray tracing
pipeline, ray query och mesh shaders i mjukvara**. Det betyder att hela
motorn, inklusive path tracern, kan byggas och bildtestas här. Långsamt (en
4K-bild tar sekunder till minuter), men komplett.

- **Enhetstester (CTest):** timeline (kapitel, era-kurva, accent,
  beat-fas), palettgenerering, bildfrekvenslås, CLI-parsning,
  `--show-info`.
- **Headless renderingstester (lavapipe):** varje kapitel renderas vid
  20 %, accent och 80 % i både då och nu till PNG. Två tester gör skillnad:
  (a) *determinism*: samma tid nådd via två olika seek-vägar ger identisk
  bildhash; (b) *innehåll*: bilden är inte svart, inte mättad, och
  då/nu skiljer sig.
- **Storyboards** sätts ihop av PNG:erna så att jag kan granska visuellt
  under utvecklingen och du kan se dem i PR:erna.
- **Hos dig (Windows, RT-GPU):** prestanda (mål: 1440p ≥ 60 fps interaktivt
  på RTX 3070-klass, 4K60 vid export), ljudsynk i öron, Windows-bygget.
- **Vulkan validation layers** körs i debug-bygget och i testerna.
- **Export:** full 4K60-film, verifierad med `verify_video_audio.py`.

## 7. Genomförande i steg (en PR per steg)

| Steg | Innehåll | Klart när |
|---|---|---|
| 0 | Repo, CMake, beroenden (SDL3, volk, VMA, Vulkan-Headers, stb), Vulkan-instans/enhet med RT-krav, swapchain, headless-läge, en compute-shader som fyller bilden, PNG-capture, CTest | Bygger på Linux här och hos dig på Windows; en PNG från lavapipe |
| 1 | Ljud + timeline: WAV-uppspelning med sampleexakt klocka, portade Python-verktyg, placeholder-låt, `show/` med cues, era, beat-grid, `--show-info`, enhetstester | Timeline-tester gröna; `--show-info` korrekt |
| 2 | Retro-pipeline: indexerad 320×200, palett + cykling, dithering, fps-lås, 4:3, CRT, 8×8-font, BOOT + scroller + PLASMA | Storyboard visar trovärdig DOS-prompt och plasma |
| 3 | Resterande retroeffekter: DOTS, TUNNEL, GLENZ, ROTOZOOM/LENS, RAYTRACE-hyllning, VECTOR CITY, FLYBY | Alla 1993-rutor |
| 4 | RT-grund: BLAS/TLAS, materialtabell, raygen/hit/miss, 1 spp + ackumulering + à-trous, export-spp, Studion med CRT-skärm som emissiv textur | Studion path-tracad i lavapipe, determinismtest grönt |
| 5 | Moderna scener: partiklar (compute), nebulosa (volym), tunnel, glas-glenz, lins/schack, kulor, stad, skepp | Alla 2026-rutor |
| 6 | Övergångar + regi: portal-kamerabanor, beat-swap, wipe, epok-skyltar, FINALE, CREDITS, bloom/grading | Hela showen spelbar start→slut med placeholder |
| 7 | Dina låtar in: pipeline, regi, lyssningspass, finjustering av klipp och drop | Kapitelstarter på uppmätta anslag; du godkänner känslan |
| 8 | Prestanda på riktig GPU, export 4K60, README (sv/en), MUSIC-NOTICE, release v1.0 | Verifierad film + release-paket |

Steg 0–6 kan göras här utan din låt. Steg 7–8 kräver dig.

## 8. Risker

- **Omfång.** En ny Vulkan-motor med path tracer är den största posten.
  Mitigering: steg 0 och 4 är små, vertikala skivor (en bild på skärmen,
  ett rum path-tracat) innan bredden byggs.
- **Denoising-kvalitet interaktivt.** Temporal ackumulering + à-trous räcker
  för stillsamma kameror men kan "ghosta" vid snabba klipp. Mitigering:
  historiken nollställs på klipp (vi vet exakt när), exporten använder
  många spp i stället.
- **lavapipe är långsamt.** Bara stillbilder och korta klipp här; prestanda
  mäts hos dig. Testerna renderar små upplösningar.
- **Windows-bygget** kan jag inte köra här. Du kör `cmake --preset windows`
  i de steg som rör plattformskod (0, 1, 8).
- **Låtens tempo/struktur.** Regin är datadriven och flyttar kapitlen, men
  ett riktigt drop vid klimax måste finnas i låten.
- **Upphovsrätt.** Egen font, egna paletter, egen geometri utöver den
  krediterade glenz-meshen. Future Crew nämns i greetings/credits.

## 9. Beslut (standardval gäller om du inte säger annat)

1. **Vulkan 1.3 + ray tracing krävs, ingen fallback.** Standard: ja.
2. **Studion som ramberättelse** med CRT-portalen. Standard: ja.
3. **DOS-boot-intro**, ~8–10 s. Standard: ja.
4. **Två epoker (1993 / 2026).** Alternativ: tre, med ett 2003-steg.
   Standard: två.
5. **CRT-shader på som standard**, togglas med C. Standard: ja.
6. **Skärmtext på engelska**, greetings får vara på svenska. Standard: ja.
7. **Credits:** förslag `CODE: CLAUDE · MUSIC: JOYOLO (SUNO) · CONCEPT: JOHN
   · GREETINGS: FUTURE CREW`. Säg vad du vill ha där.
8. **Placeholder-musik:** syntetisk WAV tills dina låtar är klara. Standard: ja.
9. **Licens:** MIT för kod (som tidigare), musik separat enligt MUSIC-NOTICE.
   Standard: ja.
10. **Placering:** fristående projekt i `evolution/` i PulseAscendant-repot.
    Beslutat.
