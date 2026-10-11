# Verifiering — Full Throttle v0.2 — 2026-10-11

## Omfattning

Åtta världar, 31 kapitel, hela låten **472,36 s** och 24 separata frekvensband. Nedanstående slutprov använder versionen med FFT-styrd geometri, partikelgrupper och tredimensionellt spektrum. Ingen mänsklig obruten genomspelning av hela demot är verifierad.

## Windows x64

**Native byggnad:** Release, C++17, MSVC 19.51, Visual Studio 18 2026 och CMake 4.4.4. SDL 3.4.18, commit `829a65d769d935c4852f8159e964312c0957260a`, källarkivets låsta SHA256 `48e15cf1a6928453d77afa955d1fdfbbca9169e2bc3d76d111acb9b8bb076d66`.

**CTest: 4/4 godkända, 6,34 s.** Core, SDL dummy-audio, incomplete_control och lifecycle. Täcker bland annat full låtlängd, loop vid 472,36 s, sökning vid 400 s, samplingsexakta hopp vid alla kapitelgränser, mute utan klockstopp, förlorad ljudutgång/återstart, ogiltig WAV, argument, ändlig geometri samt observerade fönstertillstånd. Ett avbrutet livscykeltest måste ge exit 1 och SELF-TEST INCOMPLETE.

Core kontrollerar alla 24 bands ändliga, begränsade och varierande data genom hela låten samt linjär interpolation mellan analyssteg. Python-testet `tests/test_music_analysis.py`: **2/2 godkända**, tystnad samt 100/1 000/10 000 Hz. Frekvensisoleringen kontrolleras efter att startanslagets release har lagt sig; en abrupt påslagen ton har ett verkligt bredbandigt anslag. Testet kräver rätt starkaste band och mindre än 2 % aktivitet i avlägsna band under stationär ton.

**GPU:** NVIDIA GeForce RTX 4080, drivrutin 617.42, OpenGL 3.3 core. **16/16 värld/stil-bilder** och **62/62 exakta kapitelbilder** renderade utan GL-fel. Representativa bilder från de nya världarna, glaset och 3D-spektrumet inspekterades av agenten.

**Rörelse: 31/31 kapitel godkända.** Två bilder per kapitel vid +2,0/+2,6 s. Jämförelsen använder bildens 15–80 % höjd för att undvika text; kräver medelförändring >0,012, ändring över 0,05 i minst 10 % av pixlarna och rimlig ljusnivå. Detta bekräftar föränderliga bilder, inte estetisk kvalitet eller hörbar synk.

**Kontroller:** SDL-händelser går genom ordinarie hanterare. Testet observerar helskärm av/på, fönsterstorlek 960x540, minimerat tillstånd, framåtriktad klocka under minimering, återställning och avslut. Fysisk tangentbordsanvändning är inte ett genomfört mänskligt prov.

**Full låt och ljudbackend:** Windows ordinarie backend startade från 471,8 s och körde 1,5 s. 72 960 stereoframes levererades; slutposition **0,96 s** och högsta observerade RMS 0,0457065. Detta bekräftar loop vid hela filens slut. Det bekräftar inte högtalarlyssning eller en musikaliskt sömlös skarv. SDL:s kö kompenseras, exakt hårdvarulatens är inte uppmätt.

Tidigare felvägsprov för saknad/trasig musik, ogiltig ljudbackend, saknad/ogiltig shader och exe-relativa resurser finns från v0.1. Dessa felvägar är kvar; de redovisas inte som ny separat fullständig felvägskörning. Automatiserat ljudbortfall använder en faktisk dummy-ström med injicerad förlust vid tillgänglighetsgränsen. Fysisk urkoppling av användarens ljudenhet har inte provats.

### Prestandaprov av slutrenderaren

1920x1080, modern stil, tre sekunder per värld, begärd vsync av, dolt fönster, utan ljud eller bildfångst. RTX 4080. CPU-ur runt renderanrop + bufferbyte, ingen separat GPU-timer. Initialisering före bildloopen ingår inte. Varierar med pose, bandnivåer och drivrutin; ingen garanti för andra datorer.

| Värld | Bilder/s | Medelbildtid | Högsta bildtid |
|---|---:|---:|---:|
| Photon Corridor | 443,7 | 2,24 ms | 7,30 ms |
| Plasma / Gyroid | 337,8 | 2,94 ms | 5,77 ms |
| Crystal Engine | 156,9 | 6,36 ms | 8,79 ms |
| Neon City | 471,5 | 2,10 ms | 6,42 ms |
| Fracture Machine | 368,3 | 2,70 ms | 8,01 ms |
| Cosmic Reactor | 258,8 | 3,85 ms | 7,26 ms |
| Quantum Ocean | 336,8 | 2,95 ms | 6,92 ms |
| Orbital Armada | 290,8 | 3,42 ms | 6,80 ms |

Alla åtta representativa prov nådde 60 bilder/s med marginal. Råloggar lokalt: `build/full-throttle-benchmark-{1..8}.log`; starttider 14,154,106,30,62,2,46,170 s i världarnas ordning.

## Linux x64 / WSL

**Native byggnad:** Ubuntu 24.04, GCC 13, CMake 3.28, Release. Beständig byggnad separat från temporära displayresurser.

**CTest: 4/4 godkända, 6,26 s**, på isolerad X11-server med Xvfb, Openbox och Mesa llvmpipe. Samma testfall och ELF-program som används vid paketering. `tools/test-linux-display.sh` startar egen display och stänger sina egna processer. WSLg monterar `/tmp/.X11-unix` skrivskyddad här, så körningen använde en privat mount-namespace med separat temporär `/tmp`; användarens display ändrades inte.

**GPU i WSLg:** Microsoft D3D12 / NVIDIA GeForce RTX 4080, OpenGL 4.6 core, Mesa 25.2.8. Slutversionens **62/62 kapitelbilder** renderades utan GL-fel; rörelsekontrollen gav **31/31**. En separat körning täcker båda stilarna i alla åtta världar. NVIDIA valdes per process med `GALLIUM_DRIVER=d3d12 MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA LD_LIBRARY_PATH=/usr/lib/wsl/lib`. WSLg väljer llvmpipe som standard i denna miljö. Ingen Linux-prestandagaranti ges.

**Gränser:** WSLg bekräftade tidigare inte X11-minimering och godkändes därför inte för den strikta fönsterlivscykeln. Wayland-provet bekräftade inte heller helskärmsbytet. Dessa är inte godkända livscykelprov; godkända 4/4 avser isolerad Xvfb/Openbox. Native ljudström i WSLg öppnades i v0.1; v0.2:s fulla låttransport provas med SDL dummy på Linux och verklig backend på Windows. Vanligt fysiskt Linux-skrivbord och mänsklig användning återstår.

**Systemberoenden:** ELF länkar libOpenGL.so.0, libstdc++.so.6, libm.so.6, libgcc_s.so.1, libc.so.6, libGLdispatch.so.0 och laddaren. SDL laddar tillgängliga bild- och ljudbackends dynamiskt. För Ubuntu exempelvis `libopengl0 libstdc++6 libx11-6 libxext6 libxrandr2 libxcursor1 libxi6 libxfixes3 libxss1 libwayland-client0 libxkbcommon0 libpulse0 libasound2t64`, plus fungerande OpenGL-drivrutin. Systembibliotek följer inte med tarballen. Windows använder statisk SDL/MSVC-runtime och Windows-systembibliotek.

## Musik och analysdata

- Hela MP3:n bevarad. SHA256 `fd8c915c3d7a52cb3175b3ce84bceadf99405e6ea2efa675651838161a5adff3`.
- Runtime-WAV: **22 673 280 stereoframes**, 48 kHz PCM16, **472,36 s**, utan tillagda klipp eller toningar. SHA256 `fe82b791b2d4a947de483dbdd4c567d4bd6636f38a3a62e53592914b1f286ce9`.
- **9 448 analyssteg vid 20 Hz**, tre nivå/anslagskurvor och 24 logaritmiska FFT-band, 35–18 000 Hz. 1 079 upptäckta transienttoppar; inget fast BPM eller påstådd notdetektion.
- Frekvensdata är förberäknade från just denna fil och spelas upp vid transportens klocka, även efter scenhopp. Live-RMS är ett komplement. Slutpaketet behöver inte Python eller numpy.
- Källarkivet innehåller genererad analys, runtime-WAV och original-MP3. Git ignorerar ljudfilerna.

## Evidens och acceptans

Byggnad, automation, GPU-bilder, aktiv ljudström och mänsklig upplevelse är separata nivåer. Kvar för mänsklig bedömning: helhetskvalitet i rörelse, fysisk tangentbordsanvändning, upplevd ljud/bildsynk, likhet med originalmusiken och musikalisk loopskarv. AAA-kvalitet är inget verifierat testresultat.

Projektägaren har begärt offentlig publicering av denna kostnadsfria demorelease. Musiken omfattas inte av kodens MIT-licens; kontoform och äganderätt har inte verifierats oberoende. Se `licenses/MUSIC-NOTICE.txt`. Originalkällan har bevarats separat under utvecklingen. Mindre utvecklingsbegränsning om shaderkopiering dokumenteras i README; installationen läser aktuella källresurser.

Offentliga paket, filhashar och versionsuppgifter publiceras på GitHub Releases. De ursprungliga utvecklingsloggarna följer inte med publiceringen.

## Videoexport

Ett deterministiskt 4K60-prov gav exakt 120 bilder för 2,0 s vid 3840x2160, H.264 NVENC och stereo AAC i 48 kHz. Bildorientering och kristallbild kontrollerades. Efter videoexportändringen gav CTest Windows 4/4 (5,56 s), Linux isolerad Xvfb/Openbox 4/4 (9,84 s). Full video renderas separat; slutliga metadata och hash följer GitHub-releasen.
