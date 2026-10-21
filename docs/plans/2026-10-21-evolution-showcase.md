# EVOLUTION — "så här såg det ut då, kolla nu"

Designspec och genomförandeplan för ett nytt demo i Pulse Ascendant-motorn.
Status: **utkast för granskning**. Inget av detta är implementerat än.

## 1. Idén i en mening

Ett realtidsdemo som växlar mellan **1993** (320×200, 256 färger, 486-känsla, de
klassiska effekterna från demoscenens guldålder) och **2026** (4K, HDR,
hårdvaru-raytracing, miljontals partiklar) — samma effekt, samma musikaliska
ögonblick, två epoker. Musiken är en ny Suno-låt som själv går från tracker-ljud
till modern produktion, så att både bild och ljud "växer upp" under låtens gång.

Känslan vi jagar: hakan som tappades 1993 framför Second Reality, fast den här
gången för att man ser *avståndet* mellan då och nu i samma bildruta.

## 2. Vad vi utgår ifrån (och vad vi inte rör)

Repot innehåller redan allt tungt maskineri:

| Finns i dag | Återanvänds till |
|---|---|
| SDL3/OpenGL 3.3-app, HDR-compositor, bloom, streaks, grading | Hela "nu"-bilden |
| Vulkan KHR ray queries (`hardware_rt.cpp`, `rt_scene.comp`) | Riktig raytracing i "nu"-kapitlen |
| 24-bands FFT, 100 Hz transienter, beat-tracker, musikregi (`tools/*.py`) | Synk, klipp på taktslag, kapitelstarter |
| Timeline med kapitel, accentpunkt, shots, kamerarigg | Dramaturgi och klippning |
| `--render-stream`, `render_chunked.py`, 4K60-export | Filmen till YouTube |
| 14-vertex/24-triangels glenz-mesh (Future Crew, public domain, krediterad) | Retro-glenzen |
| Befintliga världar: korridor/tunnel, gyroid-plasma, neonstad, Prism Chamber | Flera "nu"-motsvarigheter |

Vi rör **inte** det befintliga treaktsdemot: det förblir standardshow, alla
dess tester ska fortsätta passera oförändrade. EVOLUTION blir en **andra show i
samma körbara fil**, vald med `--show evolution` (och tangent **F4**).

Varför inte ett nytt projekt (t.ex. WebGPU i webbläsare)? Motorn, musikpipelinen
och exportkedjan är redan verifierade. En webbversion skulle sakna hårdvaru-RT
och göra "kolla vad vi kan göra nu"-budskapet svagare. Reuse vinner.

## 3. Dramaturgi

### 3.1 Ramberättelse: Studion

Demot öppnar och stänger i **Studion**: ett modernt, raymarchat rum med mjuka
skuggor där en 1993-PC med CRT-monitor står på ett skrivbord. På skärmen kör
retrodemot. Kameran flyger *in i* skärmen → 1993 fyller bilden. När musiken
släpper dras kameran *ut* igen, vänder sig om, och samma effekt står i rummet
(eller har blivit en hel värld) i 2026-version. Skärmen är portalen mellan
epokerna. Det ger ett konsekvent visuellt språk och förklarar sig självt utan
text.

### 3.2 Tre övergångstyper (varieras så det inte blir mekaniskt)

| Typ | Vad som händer | Används när |
|---|---|---|
| **Portal** | Kameran flyger in i/ut ur CRT-skärmen i Studion | Första och sista kapitlen, plus 2–3 nyckelkapitel |
| **Beat-swap** | Hård växling då↔nu på taktslag, först var 4:e takt, sedan var 2:a, sedan varje slag, tills "nu" vinner | Högenergikapitel, gärna in i drop |
| **Wipe** | En vertikal skarv svepar över bilden i takt med taktens fas; delad bild "1993 | 2026" | Jämförelse-kapitel där man ska *se* skillnaden sida vid sida |

Växlingspunkten i varje kapitel ligger på kapitlets **accent** (motorn letar
redan upp det starkaste anslaget 28–76 % in i kapitlet), så flippen landar på
musiken utan handjustering.

### 3.3 Kapitelplan (riktvärde ~4:30–5:00, följer låten)

Tiderna är placeholders tills låten finns; musikregin (`direct_music.py`)
flyttar kapitelstarterna till uppmätta anslag inom ±2,5 s.

| # | Kapitel | 1993 (320×200, 256 färger) | 2026 | Övergång |
|---|---|---|---|---|
| 0 | **BOOT** | Svart skärm, DOS-prompt skrivs: `C:\DEMO>EVOLUTION.EXE`, ljudkortsdetektering, "LOADING…" | — | Rak klippning till kap 1 |
| 1 | **FLYBY** | Flat-shadeat rymdskepp mot stjärnfält, sinus-scroller "WE WERE THERE IN 1993…" | Samma skepp som PBR-hjälte med volymetriska motorspår, tusentals stjärnor, lens flares, glidande kamera | Portal (ut ur skärmen, första avslöjandet av Studion) |
| 2 | **DOTS** | Några hundra punkter roterar och morfar kub → sfär | 1 000 000+ partiklar morfar kub → sfär → torusknut → logotyp, DOF, motion blur, turbulens från basen | Beat-swap |
| 3 | **PLASMA** | Palettcyklande sinusplasma | Volymetrisk 3D-plasma: raymarchad nebulosa/gyroid med emission och AO | Wipe |
| 4 | **TUNNEL** | Ringtunnel av punkter/texturremsor med vobblande centrum | Raymarchad oändlig tunnel, metalliska PBR-paneler, ljusstrålar, reflektioner | Portal |
| 5 | **GLENZ** | Transparenta flat-shadeade polygoner (den krediterade 14/24-meshen) | Riktigt glas: ray-tracad refraktion och dispersion (Vulkan), reflektioner i rummet | Beat-swap |
| 6 | **ROTOZOOM / LENS** | Roterande, zoomande schackmönster; "lins" som förstorar en bild | Oändligt schackgolv med ray-tracade speglingar, fysikaliskt korrekt glassfär som rullar över golvet | Wipe |
| 7 | **RAYTRACE** (klimax) | "Raytracade" kromkulor på schackbräde — ryckig 8–12 fps, hyllning till 1993 års *förberäknade* animationer | Äkta hårdvaru-raytracing i realtid: dussintals krom- och glaskulor, mjuka skuggor, flerstudsreflektioner, 60 fps | Beat-swap in i låtens drop |
| 8 | **VECTOR CITY** | Flat/gouraud-stad med ~200 polygoner | Neonstad med miljontals fönster, volymetrisk dimma, blöta gator med RT-speglingar | Wipe → Portal |
| 9 | **FINALE** | Delad bild: alla effekter "då" i ett rutnät | Kameran drar sig ut ur CRT:n i Studion; monitorn står kvar och spelar 1993 medan rummet exploderar i 2026-partiklar | Portal |
| 10 | **CREDITS** | Sinus-scroller med greetings (Future Crew m.fl.) | Modern typografi ovanpå; "THE END?" → "REALITY IS WHAT YOU RENDER" | — |

Varje kapitel bär också en **epok-skylt** (nedre vänstra hörnet) med *ärliga*,
beräknade siffror, t.ex. `1993 · 320×200 · 256 COLORS · 8 FPS · 512 POINTS`
mot `2026 · 3840×2160 · HDR · RAY TRACED · 60 FPS · 1,048,576 POINTS`.
Siffrorna hämtas från faktisk konfiguration, inte skrivs för hand. Inga
"1000× bättre"-påståenden i text; bilden får tala.

## 4. Teknisk design

### 4.1 Show-abstraktion (det enda större ingreppet i befintlig kod)

`timeline.cpp` hårdkodar tre akter. Vi inför en `Show`-struktur med spår,
cue-data, beat-grid och reaktivitetsdata, och en `selectShow("three-acts" |
"evolution")` som anropas före första `sampleTimeline`. Standard är
`three-acts`, så alla befintliga tester och `--show-info` är oförändrade.
EVOLUTION består av **ett** spår (låten) och en egen cue-sheet.

`TimelineFrame` får nya fält:

```
int show;        // 0 = three-acts, 1 = evolution
int effect;      // vilket då/nu-par (0..10)
float era;       // 0 = 1993, 1 = 2026, mellanvärden under wipe/portal
int transition;  // portal / beat-swap / wipe
float eraCut;    // 1 i ögonblicket epoken byts, avklingar (för blixt/grain)
```

Cue-strukturen för evolution:

```
struct Cue {double start; int effect, transition; float drive;
            const char* title; const char* thenCard; const char* nowCard;};
```

`era` räknas ut av timeline (deterministiskt, seek-säkert) från kapitlets
accent och övergångstyp, så export och interaktiv körning är identiska.

### 4.2 Retro-pipeline (nytt)

Dagens "retro"-läge är bara lägre upplösning + grov kvantisering. För
trovärdig 1993 behövs:

1. **Indexerad rendering.** Retro-effekter renderar till ett 320×200-mål och
   skriver ett **palettindex** (0–255), inte RGB. Varje effekt definierar sin
   egen palett (gradienter, 16 nyanser × 16 färger för 3D-shading etc.).
2. **Palettuppslag + cykling.** En 256×1-texturpalett per effekt; cykling
   sker via uniform-offset per intervall. Detta ger äkta palettrotation i
   plasman och tunneln.
3. **Bayer-dithering** (2×2/4×4) för effekter som räknar kontinuerliga värden.
4. **Bildfrekvenskvantisering.** Retro-tiden låses till t.ex. 35 Hz
   (`floor(t·rate)/rate`); RAYTRACE-kapitlet låses till 8–12 fps för
   "förberäknad animation"-känslan. Bilden cachas mellan uppdateringar.
5. **4:3 och icke-kvadratiska pixlar.** 320×200 visas i 4:3 (pixelaspekt
   1,2) centrerat i 16:9, nearest-neighbour.
6. **CRT-shader** (dämpad, togglas med **C**): scanlines, lätt krökning,
   fosformask, lätt bloom och vinjett. Standard på, men subtil.
7. **8×8 bitmappsfont** (egen design, ingen kopierad ROM-font) för
   DOS-prompten och scrollrarna. Befintlig 5×7-font behålls för HUD.

### 4.3 Moderna scener

Vi stannar på **OpenGL 3.3** (inga compute-shaders): partiklarna görs
**tillståndslösa** — position = f(id, tid) i vertex-shadern med curl-brus och
analytiska morph-mål (kub, sfär, torusknut, logotyp). Det är deterministiskt
vid seek och export, vilket motorn kräver, och 1 M punkter i 4K är rimligt på
RTX-klass. Nytt i GL:

- `particles.vert/frag` — morfande partikelmoln, additiv blending in i bloom.
- `studio.frag` — Studion: rum, skrivbord, PC, CRT som SDF; skärmen samplar
  retro-bilden (`uRetroFrame`) med CRT-look; mjuka skuggor och AO.
- `ship.frag` — SDF-rymdskepp (samma formspråk som befintliga fighters).
- `lens.frag` — schackgolv + glassfär, raymarchad reflektion/refraktion
  (fallback när RT saknas).
- `spheres.frag` — kromkulor på schackbräde, raymarchad fallback.
- Återanvänds med ny variant: `corridor.frag` (tunnel), `sculpture.frag`
  (plasma), `city.frag`/`act2.frag` (stad), glenz-mesh.

### 4.4 Vulkan ray tracing (två nya uppsättningar)

`hardware_rt.cpp` får två nya scenlayouter: **kulor på schackbräde** (ny
procedurell schackmaterialtyp i `rt_scene.comp`) och **glas-glenz** (meshen
som ny BLAS, glasmaterial finns). Allt annat RT-maskineri återanvänds.
Fallback till GL-varianterna sker exakt som i Akt II, med synlig märkning.

**Begränsning:** den här utvecklingsmiljön har ingen Vulkan-GPU. RT-koden
skrivs och kompileras här (SPIR-V valideras), men körs och bedöms av dig på
Windows-maskinen. Den befintliga Akt II-RT:n är referensen för hur det ska bete
sig.

### 4.5 Komposition av då/nu

Compositorn får båda bilderna (retro-målet efter palett/CRT och det moderna
HDR-målet) plus `era`, `transition`, `eraCut`:

- **Wipe:** skarv i x som följer taktfasen; tunn ljus kant på skarven.
- **Beat-swap:** val per bildruta från timeline (ingen blandning), en kort
  vit blixt och kornighet vid bytet.
- **Portal:** moderna scenen (Studion) renderar skärmen med retro-bilden;
  när skärmen fyller >98 % av vyn byter compositorn till ren retro (och
  tvärtom). Kamerabanan är författad per kapitel.

### 4.6 Musik och pipeline

Samma kedja som i dag, generaliserad till en show med ett spår:
`tools/prepare_show.py --show evolution <låt.mp3|wav>` → 48 kHz stereo PCM16
WAV → `analyze_music.py` (24 band) → `analyze_reactivity.py` (100 Hz
anslag) → `beat_grid.py` (takt/takter) → `direct_music.py` (kapitelstarter)
→ genererade headers (`music_data_evolution.hpp`, `cue_data_evolution.hpp`,
`beat_data_evolution.hpp`). Provenance skrivs till `music/` och
`licenses/MUSIC-NOTICE.txt` enligt befintlig mall (Suno-ID, SHA-256).

**Under utveckling, innan din låt är klar,** använder jag en syntetisk
placeholder-WAV (klickspår + enkla syntar i rätt längd och tempo) så att hela
tidsmaskineriet kan byggas och testas. När riktiga låten kommer körs
pipelinen om och kapitlen hamnar på dina anslag. (Alternativ: generera en
tillfällig låt via din Suno-koppling — kostar credits, så bara om du vill.)

### 4.7 Styrning och CLI

- `--show evolution` väljer showen; `--show-info` rapporterar vald show.
- **F4** hoppar till EVOLUTION i interaktiv körning; **1–9, 0** hoppar till
  kapitel inom showen; **Tab** tvingar då/nu manuellt (för inspektion); **C**
  togglar CRT-shadern.
- Exportverktygen (`render_chunked.py`, `render_video.py`) får `--show`.

## 5. Önskemål på låten (Suno)

Detta är det enda jag behöver från dig utöver beslut nedan.

- **Längd:** 4:00–5:00. Allt över ~5:30 gör demot segt.
- **Tempo:** konstant 4/4, gärna 128–140 BPM. Beat-trackern följer drift men
  inte halvtempo-växlingar; tempobyten gör klippningen opålitlig.
- **Struktur som berättar evolutionen:**
  - 0:00–0:30 — chiptune/tracker-intro (FastTracker/Amiga-känsla, kvadratvågsleads,
    8-bitars trummor), tydlig nedräkning/upptakt för BOOT + FLYBY.
  - 0:30–2:00 — 90-tals demoscen-trance/techno, tydliga takter (DOTS, PLASMA, TUNNEL).
  - 2:00–2:45 — bryggan: modern produktion börjar ta över (GLENZ, LENS).
  - ~2:45–3:00 — **uppbyggnad och tydligt drop** (RAYTRACE-klimaxet landar här).
  - 3:00–4:15 — stort, modernt, filmiskt (VECTOR CITY, FINALE).
  - 4:15–slut — utro med återblick på intro-temat (CREDITS).
- Tydliga anslag på downbeats; undvik lång ambient utan puls (tappas av
  beat-trackern).
- **Leverans:** WAV eller högsta MP3-kvalitet + Suno-ID för krediterna.

Förslag på stilprompt att utgå ifrån:
> "Evolving instrumental: starts as 1993 PC tracker chiptune (square leads,
> 8-bit drums, FastTracker vibe), grows into 90s demoscene trance, then
> transforms into modern cinematic EDM with huge drop at 2:50, constant 134 BPM,
> 4/4, epic outro reprising the chiptune theme. No vocals."

## 6. Verifiering

- **Enhetstester (CTest):** showval, kapitelräkning och monotona starter,
  `era`-kurvans determinism (samma värde vid seek), palettgenerering,
  bildfrekvenskvantisering, `--show-info` för evolution, oförändrade resultat
  för three-acts.
- **Headless bildprov här i containern:** Xvfb + Mesa llvmpipe finns. Varje
  kapitel fångas vid 20 %, accent, 80 % i både då och nu → storyboard-PNG
  som jag granskar visuellt under utvecklingen.
- **Python-tester** för analys/regi på placeholder- och sedan riktig låt.
- **Hos dig (Windows, RT-GPU):** RT-layouterna, prestanda (mål: 1440p ≥ 60 fps
  interaktivt på RTX 3070-klass, 4K60 vid export), ljudsynk i öron.
- **Export:** full 4K60-film med `render_chunked.py`, verifierad med
  `verify_video_audio.py` som i dag.

## 7. Genomförande i steg (varje steg = en PR som går att granska)

| Steg | Innehåll | Klart när |
|---|---|---|
| 0 | Miljö: bygga befintligt projekt headless här (apt-beroenden, Xvfb/llvmpipe), bekräfta capture-pipelinen | `ctest` grönt, en BMP-capture från treaktsdemot |
| 1 | Show-abstraktion + EVOLUTION-skelett: `--show`, F4, cue-sheet med placeholder-musik, `era`-logik, tester | three-acts-tester oförändrade; evolution i `--show-info` |
| 2 | Retro-pipeline: indexerat 320×200, palett + cykling, dithering, 35 Hz-lås, 4:3, CRT-shader, 8×8-font, BOOT + scroller | Storyboard visar trovärdig DOS-prompt och plasma |
| 3 | Resterande retroeffekter: DOTS, TUNNEL, GLENZ, ROTOZOOM/LENS, RAYTRACE-hyllning, VECTOR CITY, FLYBY | Alla 1993-rutor i storyboarden |
| 4 | Moderna GL-scener: partiklar, Studion + CRT-portal, skepp, lins/schack, kulor-fallback, varianter av tunnel/plasma/stad | Alla 2026-rutor i storyboarden (GL-vägen) |
| 5 | Övergångar + regi: portal-kamerabanor, beat-swap, wipe, epok-skyltar, FINALE, CREDITS | Hela showen spelbar start→slut med placeholder-låt |
| 6 | Vulkan RT: kulor på schack, glas-glenz, SPIR-V-validering | Du bekräftar RT på Windows; fallback identiskt märkt |
| 7 | Din låt in: pipeline, regi, lyssningspass, finjustering av klipp och drop | Kapitelstarter på uppmätta anslag; du godkänner känslan |
| 8 | Export 4K60, README (sv/en), MUSIC-NOTICE, release v0.7.0 "Evolution", ev. trailer | Verifierad film + release-paket |

Steg 1–5 kan göras helt här utan din låt. Steg 6–7 kräver dig.

## 8. Risker och hur de hanteras

- **Timeline-refaktorn** rör kärnan. Mitigering: three-acts förblir
  standard, hela befintliga testsviten måste vara grön efter steg 1.
- **RT kan inte köras här.** Mitigering: liten RT-yta (två layouter), SPIR-V
  valideras, GL-fallback bedöms visuellt, du testar på Windows.
- **Låtens tempo/struktur** avviker från önskemålen. Mitigering: regin är
  datadriven; kapitlen flyttar sig. Men ett riktigt drop vid klimax behöver
  finnas i låten.
- **Upphovsrätt.** Vi återskapar *effektprinciper*, inte Second Realitys
  grafik, musik, logotyp eller kod (utöver den redan krediterade
  glenz-geometrin). Egen 8×8-font. Future Crew nämns i greetings/credits.
- **llvmpipe är långsamt.** Bara stillbilder här; prestanda mäts hos dig.
- **Windows-bygget** kan jag inte köra här; du kör `cmake --preset windows`
  per steg som rör plattformskod.

## 9. Beslut jag behöver från dig

Jag har satt ett standardval på varje punkt och bygger enligt det om du inte
säger annat.

1. **Samma körbara fil, ny show (`--show evolution`, F4).** Treaktsdemot
   förblir standard. *(Alternativ: gör EVOLUTION till standardshow.)*
2. **Studion som ramberättelse** med CRT-portalen. Ja.
3. **DOS-boot-intro.** Ja, ~8–10 s.
4. **Två epoker (1993 / 2026).** *(Alternativ: tre, med ett 2003-steg à la
   640×480 Gouraud/texturer. Ger mer "evolution", kostar ~30 % mer arbete.
   Kan läggas till i enstaka kapitel senare.)*
5. **CRT-shader på som standard**, dämpad, togglas med C.
6. **Text på skärmen på engelska** (scenkonvention); greetings får vara på svenska.
7. **Credits:** förslag `CODE: CLAUDE · MUSIC: JOYOLO (SUNO) · CONCEPT: JOHN
   · GREETINGS: FUTURE CREW`. Säg vad du vill ha där.
8. **Placeholder-musik:** syntetisk WAV (gratis) tills din låt är klar.
   *(Alternativ: tillfällig Suno-låt via din koppling.)*
9. **Släppnamn:** `v0.7.0 "Evolution"` och kapiteltitel `EVOLUTION / 1993 → 2026`.

När du svarat på (eller godkänt) ovan skriver jag den detaljerade
implementationsplanen för steg 0–2 och börjar koda.
