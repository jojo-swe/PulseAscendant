# Från Second Reality till Full Throttle

Original: Future Crew, 1993. https://github.com/mtuomi/SecondReality, referenscommit `071a82eddb7a7390dbaa491a8a473e0e923a55c2`. Originalkällan har bevarats separat under utvecklingen och länkas ovan.

| Original | Bevarad grund | Modern omarbetning |
|---|---|---|
| `TUNNELI/TUN10.PAS`, `TUNNELI/TUNN.PAS` | Ringar med sinusförskjuten mittpunkt och perspektiv genom division med djup | `scene_math.cpp` skapar 160 x 96 punkter med nya frekvenser och hastigheter. `corridor.frag` sphere-tracar den böjda väggen; `tunnel.*` rasteriserar ljuspartiklar. |
| `PLZPART/PLZ.C`, `PLZSINI` | Summan av sinusfält med modulerade koordinater och rörliga faser | `sculpture.frag` använder nya 3D-frekvenser på en gyroid med självskuggning, AO, frekvensstyrda ådror och golvreflektion. Originalets tabeller och VGA-paletter kopieras inte. |
| `GLENZ/MAIN.C`, `points[]`, `epolys[]` | Samma 14 vertexpositioner normaliserade med `100*ZZZ`, samma 24 trianglar | `glenzMesh` behåller geometrin; `engine.frag` klipper strålar analytiskt mot facettplanen och tillför brytande glas samt tre roterande metallringar med 48 kragar vardera. Retro använder transparenta rasteriserade facetter. |

## Nytt i v0.2

Fem världar är nya och har ingen direkt motsvarande originalkod: `city.frag` (upprepade byggnader), `structure.frag` (vikta SDF-former), `cosmic.frag` (volymetrisk skiva/korona), `ocean.frag` (raymarchade vågor och klot) och `armada.frag` (upprepade skepp). `flight.vert` och `swarm.vert` skapar samtidiga 3D-spår från vertex-ID. `spectrum.vert` bygger 96 frekvensstyrda trådmodellsstaplar i fyra radiella kransar. Partiklar och spektrum är additiva lager utan djupocklusion mot SDF-scenerna.

`timeline.cpp` regisserar 31 kapitel över hela låtens 472,36 s. Klipp flyttas högst 0,35 s mot uppmätta transienter. Fyra kameravarianter, dollyrörelser, bankning och snabbare rörelsetid skapar variation även när en värld återkommer. Animationens rörelsetid och musikens analysklocka är separata: att kameran accelererar gör inte musikanalysen snabbare.

## Musikanalys

`tools/analyze_music.py` läser den fulla PCM-filen och genererar `src/music_data.hpp` samt `music/analysis.json`. RMS, 35–180 Hz bas och positiv spektral flux ger nivå och anslag. Dessutom analyseras **24 logaritmiska band, 35–18 000 Hz**, med centrerad Hann-FFT: 8 192 samplingar vid 48 kHz, ett analyssteg per 50 ms. Bandnivåer normaliseras med en gemensam miniminivå för att undvika att nästan tysta band förstärks, jämnas med attack/release och kvantiseras till byte.

Renderaren interpolerar bandkurvorna vid aktuell ljudposition och skickar alla 24 värden till GPU:n. De styr olika geometridimensioner, materialzoner och partikelgrupper. Det är en riktig frekvensanalys av den fasta låten, förberäknad för reproducerbar körning. Det är ingen analys av mikrofon/systemljud, ingen nottranskribering och inget antagande om fast BPM. Live-RMS från SDL finns kvar som kompletterande ljusnivå. Mute stoppar inte analysklockan.

Scenvalet följer transportens samplingar med kompensation för SDL:s kö. Exakt hårdvarulatens och upplevd ljud/bildsynk är inte uppmätta. Spektrum och reservtidslinje fungerar även i SILENT MODE.

## Rendering och approximationer

HDR-mellanbuffer RGBA16F, sex separabla bloompass på halv upplösning, filmisk tonmappning, gammaomvandling, begränsad radiell oskärpa och färgseparation. Retro renderar världen i 320x200 med närmaste pixel vid uppskalning; texten använder utdataupplösning. Det är ingen DOS-emulator.

Ljusspridning integrerar emissiva närhetsfält längs primärstrålen; ingen komplett deltagande mediemodell. SDF-golvreflektioner använder en andra stråle. Kristallglaset kombinerar Fresnelreflektion, brytning vid in/utträde, Beer-absorption och två brytningsindex (1,45/1,48) som färgspridningsapproximation. Högst en intern reflektion tillåts. Kristallen i sekundär golvreflektion förenklas till miljöljus. Miljöljusen är analytiska. Ingen hårdvaruraytracing, path tracing eller AI-uppskalning används.

Future Crew krediteras för originaldemo, geometri och effektidéer. Se `licenses/FutureCrew-Unlicense.txt`. Den nya koden är en selektiv remix och utbyggnad, ingen återställning av hela originalprogrammet.
