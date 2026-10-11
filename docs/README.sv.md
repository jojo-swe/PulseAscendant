# Second Reality / Full Throttle — v0.2

Ett fristående C++17/SDL3/OpenGL-demo för Windows och Linux. Åtta procedurala 3D-världar, 31 kapitel och hela **Luminous Flow: 7 minuter 52,36 sekunder**. Future Crews Second Reality (1993) är utgångspunkten för tunneln, plasmatekniken och kristallgeometrin. Övriga världar och den nya koreografin är nyskrivna.

## Kör

**Windows:** packa upp hela zipfilen och starta `SecondRealityRemix.exe`. Behåll `assets/` bredvid programmet. Windows 10/11 x64 och OpenGL 3.3 krävs. SDL och MSVC-runtime är statiskt länkade.

**Linux:** packa upp tarballen och kör `./SecondRealityRemix`. Byggt för Ubuntu 24.04 x64, glibc 2.39 och GCC 13:s libstdc++. Kräver X11/Wayland, grafikdrivrutin och systembiblioteken i `VERIFICATION.md`.

Demot startar i 1280x720 och loopar hela låten. Musiken har ingen 90-sekundersgräns, klippning eller tillagda toningar. Programmet behöver inget nätverk eller konto.

| Tangent | Funktion |
|---|---|
| F11 | Helskärm / fönster |
| 1–8 | Hoppa med grafik och musik till respektive världs första kapitel |
| Vänster / höger pil | Föregående / nästa kapitel |
| Tab | Modern / retrostil i 320x200 |
| M | Ljud av/på; tidslinjen fortsätter |
| Esc | Avsluta |

## Musik som driver grafik

24 logaritmiska frekvensband mellan **35 Hz och 18 kHz** kommer från FFT-analys av den faktiska musikfilen. Kurvorna förberäknas vid 20 Hz, jämnas med attack/release och interpoleras vid ljudtransportens uppspelningsposition. Bas, mellanregister och diskant kan därmed röra olika objekt samtidigt. Det är analys av den medföljande låten; inget mikrofonljud eller systemljud fångas.

- Bas och anslag påverkar puls, stora former och ljusstyrka.
- Separata band styr ringar, fönster, gyroidens material och tjocklek, havsvågor, svävande klot och skeppens motorljus.
- Partikelgrupper har egna band för radie, spårlängd och intensitet.
- Tre världar har dessutom fyra roterande kransar med 24 tredimensionella spektrumstaplar vardera.
- 31 redigerade kapitel med klipp flyttade till närliggande uppmätta transienter. Fyra kameravarianter växlar inom kapitlen; ingen fast BPM antas.

Spektrumanimationen följer samma klocka även efter scenhopp och mute. Vid saknat ljud används en reservklocka och en synlig SILENT MODE-markering.

## Världar och samtidiga lager

| Värld | Grafik |
|---|---|
| Photon Corridor | Böjd raymarchad tunnel, lysande ribbor, 15 360 ringpartiklar och 4 000 fartspår |
| Plasma / Gyroid | Genombruten 3D-form, frekvensstyrda ådror, AO, mjuka skuggor, golvreflektion och 8 000 orbitala spår |
| Crystal Engine | Originalets facettgeometri med brytning, Fresnelreflektion och färgspridning; tre GGX-belysta metallringar och 8 000 spår |
| Neon City | Snabb flygning genom upprepade 3D-byggnader med frekvensstyrda fönster och höjder, spår och neonräls |
| Fracture Machine | Vikta SDF-strukturer, pulserande portar, rollande kamera och fartspår |
| Cosmic Reactor | Volymetrisk ackretionsskiva, korona, nebulosa, 24 000 orbitala spår och ett radiellt 3D-spektrum |
| Quantum Ocean | Frekvensstyrda vågor, reflekterad himmel, svävande klot, aurora och låg flygning |
| Orbital Armada | Upprepade 3D-skepp med kropp, vingar, cockpit och frekvensstyrda motorljus; kamera genom flottan |

Alla världar kombinerar geometri och partikellager i samma bild. HDR, bloom, begränsad radiell rörelseoskärpa och färgseparation binder ihop uttrycket. Detaljer och approximationer finns i `SOURCE_MAP.md`.

## Bygg

Källarkivet innehåller WAV, hela käll-MP3:n och genererad analysdata. Git ignorerar de två ljudfilerna. För att återskapa dem och analysen: `tools/prepare_music.ps1 -SourcePath 'Luminous Flow.mp3'` med ffmpeg och Python + numpy. Dessa analysverktyg behövs inte för att köra programmet eller bygga ett komplett källarkiv.

SDL 3.4.18 hämtas från en låst commit med verifierad SHA-256. En lokal kopia kan anges med `-DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL`. Första hämtningen behöver nätverk.

Windows med Visual Studio 2026 och CMake 3.25+:

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
./tools/smoke-test.ps1
./tools/package.ps1
```

Visual Studio 2022 kan väljas manuellt med `-G 'Visual Studio 17 2022' -A x64`; det generatorvalet är inte testat här.

Ubuntu 24.04:

```sh
sudo apt install build-essential cmake pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev libasound2-dev libpulse-dev
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
build/linux/SecondRealityRemix --smoke-test --no-audio --capture-dir build/captures-linux
bash tools/package.sh build/linux
```

## Diagnostik och verifiering

```sh
SecondRealityRemix --help
SecondRealityRemix --time 400 --duration 5 --width 1920 --height 1080
SecondRealityRemix --self-test --duration 15
SecondRealityRemix --smoke-test --no-audio --capture-dir captures
SecondRealityRemix --tour-test --no-audio --capture-dir tour
python tools/verify-motion.py tour motion.json
python tests/test_music_analysis.py
```

Smoke-testet fångar åtta världar i två stilar. Tour-testet fångar två tidpunkter i vart och ett av de 31 kapitlen. Bildjämförelsen kontrollerar rörelse och ljusnivå utanför textområdet. Den är inget mått på konstnärlig kvalitet. FFT-testet använder tystnad och rena bas-, mellanregister- och diskanttoner för att kontrollera frekvensseparationen.

`--duration` stänger av begärd vsync. Loggade bildtider mäter renderanrop och bufferbyte med CPU-ur, inte rena GPU-tider. Kontrolltestet injicerar SDL-händelser och observerar verkliga fönstertillstånd; det visar ett fönster även med `--hidden`. Avbrutet test ger felkod. Aktivera `-DREMIX_RUNTIME_TESTS=ON` för fyra CTest-prov, inklusive fönsterlivscykel. De kräver en fungerande display.

På Linux kan `bash tools/test-linux-display.sh build/linux` använda en isolerad Xvfb/Openbox-display. WSLg-sessionen bekräftade inte minimering och räknas inte som godkänd för den strikta livscykeln. GPU-rendering och ljud har separata prov. Här valde WSLg llvmpipe som standard; NVIDIA kunde väljas per process med `GALLIUM_DRIVER=d3d12 MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA LD_LIBRARY_PATH=/usr/lib/wsl/lib`. Vanligt Linux-skrivbord återstår att prova.

Känd utvecklingsbegränsning: enbart shaderändringar garanterar inte POST_BUILD-kopiering till exe-katalogen. Kopiera då `assets/` dit manuellt eller kör `cmake --install` till staging. Paketeringen installerar aktuella källresurser.

Uppmätta resultat och acceptansgränser: `VERIFICATION.md`. Visuell helhet, upplevd synk, musikalisk likhet och loopskarv behöver mänsklig bedömning. Ingen obruten mänsklig genomspelning av hela låten har verifierats.

## Krediter och licenser

Originaldemo, effektidéer och glenz-geometri: **Future Crew**. Källa: https://github.com/mtuomi/SecondReality, commit `071a82eddb7a7390dbaa491a8a473e0e923a55c2`, public domain/Unlicense. Originalträdet är separat och oförändrat.

SDL: https://libsdl.org, zlib-licens. Licenstexter i `licenses/`. Nyskriven remixkod: MIT. Musik: användarens Suno-export; metadata och hashar i `music/asset-receipt.json` och `music/analysis.json`. Musiken ingår i den kostnadsfria demoreleasen på projektägarens begäran, omfattas inte av MIT och behåller sina tillämpliga Suno-villkor. Se `licenses/MUSIC-NOTICE.txt`.
