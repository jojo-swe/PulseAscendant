# Pulse Ascendant: källor och renderingsvägar

Pulse Ascendant är ett fristående tre akter långt demo med egen identitet, 16 världar och 76 kapitel. Projektets första utvecklingssteg utgick från utvalda idéer i Future Crews *Second Reality* från 1993. Originalkällan är bevarad separat: [mtuomi/SecondReality](https://github.com/mtuomi/SecondReality), referenscommit `071a82eddb7a7390dbaa491a8a473e0e923a55c2`, Unlicense/public domain. Projektet är ingen officiell Future Crew-utgåva eller fullständig port.

## Originalets spår i akt I

| Original | Bevarad grund | Modern omarbetning |
| --- | --- | --- |
| `TUNNELI/TUN10.PAS`, `TUNNELI/TUNN.PAS` | Ringar med sinusförskjuten mittpunkt och perspektiv genom division med djup | `scene_math.cpp` skapar 160 × 96 punkter med nya frekvenser och hastigheter. `corridor.frag` sphere-tracar en böjd vägg; `tunnel.*` rasteriserar ljuspartiklar. |
| `PLZPART/PLZ.C`, `PLZSINI` | Summerade sinusfält, modulerade koordinater och rörliga faser | `sculpture.frag` använder nya 3D-frekvenser på en gyroid med skuggning, AO, frekvensstyrda ådror och golvreflektion. Originalets tabeller och VGA-paletter kopieras inte. |
| `GLENZ/MAIN.C`, `points[]`, `epolys[]` | Samma 14 normaliserade vertexpositioner och 24 trianglar | `glenzMesh` behåller geometrin. `engine.frag` kombinerar analytiska facettplan, brytande glas och tre metallringar. Retro använder rasteriserade facetter. |

Fem ytterligare akt I-världar saknar direkt motsvarande originalkod: `city.frag`, `structure.frag`, `cosmic.frag`, `ocean.frag` och `armada.frag`. `flight.vert` och `swarm.vert` skapar samtidiga spår från vertex-ID. `spectrum.vert` bygger 96 frekvensstyrda trådmodellsstaplar i fyra kransar. Dessa OpenGL-partikellager har inte full djupocklusion mot de raymarchade SDF-världarna.

## Tre akter och en ljudklocka

`act1.hpp`, `act2.hpp` och `act3.hpp` definierar 31, 21 respektive 24 kapitel. `timeline.cpp` använder de musikregisserade startpunkterna i `cue_data.hpp`, väljer rätt aktanalys och håller musikpositionen separat från animeringens rörelsetid. Snabbare kamerarörelse gör därför inte musiken snabbare.

| Akt | PCM-frames vid 48 kHz | Tid | Global första frame |
| --- | --- | --- | --- |
| I | 14 400 000 | 300 s | 0 |
| II | 13 916 160 | 289,92 s | 14 400 000 |
| III | 14 340 480 | 298,76 s | 28 316 160 |
| Hela demot | 42 656 640 | 888,68 s | 0 |

Det sammanfogade runtime-ljudet heter fortfarande `assets/music/second-reality-three-acts.wav`; filnamnet är en intern resurs, inte produktnamnet. Inga tysta frames läggs in vid aktgränserna. Luminous Flows första 299 avkodade sekunder bevaras sampleidentiskt, följt av en sekunds uttoning. De två senare låtarna används hela i normal hastighet. Original-MP3 och tidigare publicerade versioner förblir bevarade.

`tools/prepare_show.py` verifierar källhasharna, avkodar tre WAV-filer, skapar den sammanfogade filen och kör analys samt musikregi. `tools/prepare_music.ps1` är PowerShell-ingången. Källarkivet innehåller tre MP3 och fyra WAV; runtimepaketen behöver bara den sammanfogade WAV-filen. Git ignorerar ljudfilerna, men testsviten för källbygget behöver de tre aktfilerna och den sammanfogade filen.

## Musikanalys och scenbyten

`tools/analyze_music.py` genererar `music_data.hpp`, `music_data_act2.hpp` och `music_data_act3.hpp` samt separata JSON-receipts. Centrerad Hann-FFT använder 8 192 samplingar vid 48 kHz och ett analyssteg per 50 ms. Analysen ger RMS-energi, bas, positiv spektral flux och 24 logaritmiska frekvensband från 35 till 18 000 Hz. Bandnivåer normaliseras, jämnas med attack/release och kvantiseras till byte.

`tools/direct_music.py` söker uppmätta anslag och bestående spektrala förändringar inom ±2,5 sekunder från redaktionella mål. Dynamisk programmering väljer en sammanhängande följd med 5–18 sekunders kapitelgränser; ingen fast BPM antas. Resultatet sparas i `cue_data.hpp` och `music/direction.json`. Kamera- och materialhändelser fortsätter inom kapitlen. Lyssning behövs fortfarande för att bedöma konstnärligt lämpliga klipp.

Renderaren interpolerar varje akts kurvor mot ljudpositionen. Det är reproducerbar analys av de medföljande låtarna, inte mikrofonanalys, nottranskribering eller systemljudsinspelning. Mute stoppar inte analysklockan. SDL-kökompensation och reservklocka hanteras i ljudspelaren; exakt fysisk ljudlatens och upplevd synk är separata acceptansfrågor.

## Vulkan-hårdvarutracing i akt II och III

`hardware_rt.cpp` laddar Vulkan dynamiskt och kontrollerar varje enhets egna krav. Renderaren använder `VK_KHR_acceleration_structure`, `VK_KHR_deferred_host_operations`, buffer device addresses och `VK_KHR_ray_query`.

Fem statiska triangel-BLAS representerar box, triangulerad sfär, oktaeder, ring och dynterräng. En TLAS byggs för de aktuella animerade instanserna, med en hård budget på 2 048. `hardware_rt.cpp` skapar kromkatedral, regnstad, fabrik och prismakammare. `act3_rt.inl` skapar keramiska vandrare, förgrenad vegetation, flytande sten och finalens kristallvingar, ringar och flotta. Spektrumstaplar och många emissiva objekt ingår i den verkliga TLAS:en.

`rt_scene.comp` använder KHR-ray queries för primär synlighet, skuggor och begränsad reflektion/brytning. Varje prov har högst fem ytträffar. Glas har ett förenklat brytningsindex och totalreflektionsväg; kvarvarande energi vid gränsen ersätts med begränsat miljöljus. Det är ingen opartisk path tracer, ingen allmän GI-lösning och ingen fullständig fysisk glasmodell. Modern upplösning använder två deterministiska subpixelprover; 320×200 använder ett. Compositorn kompletterar med adaptiv kantutjämning, utan temporal historik eller temporal denoising.

HDR-resultatet skrivs i Vulkan-enhetsminne, kopieras till host-readback med preferens för cachebart värdminne och laddas upp till OpenGL. Detta är en stagingväg per bild, inte zero-copy mellan API:erna. Resize kan bygga om output- och readback-resurser. Prestanda varierar med upplösning, scener och drivrutin; exporterad 4K60 är inte ett löfte om interaktiv 4K60.

Ändrad compute-källa kräver ny SPIR-V:

```sh
glslangValidator -V -Os --target-env vulkan1.2 assets/shaders/rt_scene.comp -o assets/shaders/rt_scene.comp.spv
spirv-val --target-env vulkan1.2 assets/shaders/rt_scene.comp.spv
```

SDK-verktygen är utvecklingsberoenden. Körningen använder den medföljande kompilerade shadern. En separat `remix_rt_probe` kan byggas med `REMIX_BUILD_RT_PROBE=ON`; den visar enhetskapacitet, vilket ska skiljas från renderad RT-evidens.

## OpenGL, compositing och begränsningar

OpenGL 3.3 förblir fönster- och presentationsväg. Akt I använder rasterisering, SDF-raymarching och analytiska skärningar. `act2.frag` och `act3.frag` är separata fallback-världar för senare akter. Deras SDF-geometry är inte identisk med Vulkan-mesharna, och deras miljöreflektioner återskapar inte full synlighet för reflekterade rörliga objekt. Backend-märkningen ska skilja dessa vägar åt.

HDR, bloom, filmisk tonmappning, gamma, begränsad radiell oskärpa och färgseparation binder ihop lagren. Retro renderar världen vid 320×200 och skalar med närmaste pixel; texten använder utdataupplösningen. Det är ingen DOS-emulator eller AI-uppskalning.

Källgranskning, shaderkompilering, native körning, visuella fångster, prestanda och oberoende kvalitetsbedömning är olika evidensgrader. [VERIFICATION.md](VERIFICATION.md) äger de aktuella resultaten. Äldre v0.2-prov bevisar inte automatiskt den nya treaktsversionen eller native Linux-RT.

Future Crew-krediter och originallicens finns kvar i `licenses/FutureCrew-Unlicense.txt`. Ny kod är MIT. Musikens separata ursprung och villkor finns i [MUSIC-NOTICE.txt](../licenses/MUSIC-NOTICE.txt) och respektive receipt under `music/`.
