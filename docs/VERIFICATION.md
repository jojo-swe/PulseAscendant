# Pulse Ascendant v0.3 — verifiering

## Spelbar omfattning

Tre akter, 16 världar och 76 kapitel över **888,68 sekunder (14:48,68)**. Akt I är den nya femminutersredigeringen av Luminous Flow. Akt II använder hela Adventurous Uplift och akt III hela Rigid Arpeggios, båda i normal hastighet. Originalfilerna och v0.2:s publicerade resurser är bevarade. Historiska resultat finns separat i [VERIFICATION-v0.2.0.md](VERIFICATION-v0.2.0.md).

| Akt | PCM-stereoframes | Startframe | Längd | Kapitel |
| --- | ---: | ---: | ---: | ---: |
| I | 14 400 000 | 0 | 300 s | 31 |
| II | 13 916 160 | 14 400 000 | 289,92 s | 21 |
| III | 14 340 480 | 28 316 160 | 298,76 s | 24 |
| Totalt | 42 656 640 | 0 | 888,68 s | 76 |

Alla ljudfiler är stereo PCM16 vid 48 kHz. Den gemensamma runtime-WAV:en är cirka 171 MB, under läsarens 256 MiB-gräns. Uppspelningen lagrar cirka 341 MB avkodade float-samplingar; WAV-inläsningen har dessutom en tillfällig bytebuffer. Detta är en minnesbaserad transport, ingen strömmad ljudavkodare.

## Native byggnad och automatiska prov

Windows x64: C++17, MSVC 19.51 / Visual Studio 18 2026, CMake 4.4.4, statiskt länkad SDL 3.4.18 och MSVC-runtime. Linux x64: Ubuntu 24.04, GCC 13, CMake 3.28, samma SDL-version.

- **Windows CTest: 6/6 godkända.**
- **Linux CTest: 6/6 godkända** på isolerad Xvfb/Openbox med Mesa llvmpipe.
- **FFT: 2/2 Python-prov godkända**, med tystnad och rena 100/1 000/10 000 Hz-toner.
- **Musikregi: 3/3 syntetiska Python-prov godkända**, inklusive tydliga anslag, tystnad och omöjliga kapitelintervall.
- **tests/test_show.py: godkänt** för tre akter, titel, 76 kapitel, 16 världar, käll-/runtimehashar, sample-identitet vid båda skarvarna och nekad export förbi slutet.
- SPIR-V kompilerad med glslangValidator -V -Os --target-env vulkan1.2 och godkänd av spirv-val.

CTest täcker aktlängder, kapitelgränser, fyra kameravarianter i varje kapitel, spektruminterpolation, ljudsökning och mute, båda aktövergångarna, slutloop, felaktig musiklängd och observerade fönstertillstånd. Ett avbrutet kontrollprov måste ge felstatus. Ingen fysisk tangentbordsgenomspelning eller obruten mänsklig lyssning av hela demot är verifierad.

Linux-displayprovet använder egen Xvfb/Openbox i en privat mount-namespace. Det ändrar inte användarens WSLg-display. WSLg:s tidigare misslyckade minimeringsprov räknas inte som godkänd livscykel.

## Windows GPU och rendering

NVIDIA GeForce RTX 4080, drivrutin 617.42, OpenGL 3.3 core och Vulkan KHR ray queries.

**32/32 scen/stil-bilder** renderade med strikt RT och Vulkan-validering utan rapporterade GL- eller Vulkan-valideringsfel. De åtta världarna i akterna II–III använder den riktiga RT-backenden; akt I använder OpenGL. Backendräknaren bekräftade 16 RT-bilder i smoke-provet.

**152/152 exakta kapitelbilder** renderade med strikt RT och Vulkan-validering. Backendräknaren bekräftade 90 RT-bilder. Rörelseprovet godkände **76/76 kapitel**. Det använder +2,0/+2,6 s i varje kapitel, bildens 15–80 procent i höjd, medelförändring över 0,012, mer än 10 procent ändrade pixlar och rimlig ljusnivå.

Den separat tvingade OpenGL-reservvägen renderade också alla 152 kapitelbilder och gav **76/76 godkända rörelseprov**. Referensresultat finns i [RT-rapporten](evidence/v0.3.0/motion-rt.json) och [fallbackrapporten](evidence/v0.3.0/motion-fallback.json). Dessa mätningar visar föränderliga, icke-tomma bilder; de mäter inte konstnärlig kvalitet.

RT-backenden bygger fem triangel-BLAS och en TLAS med rörliga instanser. Ray queries används för primärsynlighet, skuggor, reflektion och brytning. Upp till fem ytmöten är en begränsad materialmodell, inte en unbiased path tracer. Analytisk miljöbelysning och förenklad dimma används; generell GI och temporal denoising saknas.

Output, material och uniformdata ligger i Vulkan-enhetsminne. Kopieringsbarriärer, fence och cachebart/coherent stagingminne ordnar överföringen till CPU:n och vidare till OpenGL HDR. Enheter som enbart erbjuder CPU/mjukvarurendering godtas inte som hårdvaru-RT. Saknad loader eller lämplig GPU ger synligt OpenGL-reservläge. --require-rt och --rt-validation ger strikt fel i stället.

Storlek kontrolleras mot vald enhets maxStorageBufferRange före allokering och descriptoruppdatering. Kärnprovet täcker 4K inom 128 MiB och en större viewport som överskrider gränsen.

## Interaktiv RT-prestanda

Tre sekunder per representativ värld, modern stil, full upplösning, två RT-samplingar per pixel, dolt fönster, ljud av och begärd vsync av. CPU-ur runt renderanrop och bufferbyte, inklusive RT/GL-överföringen; ingen separat GPU-timer. Initialisering före bildloopen ingår inte. Resultaten är korta stickprov, inte en garanti för hela demot eller andra datorer.

| Värld | 1280×720 fps | 1920×1080 fps |
| --- | ---: | ---: |
| Chrome Cathedral | 48,5 | 27,0 |
| Rain District | 37,0 | 19,1 |
| Orbital Foundry | 38,2 | 20,8 |
| Prism Chamber | 43,9 | 24,9 |
| Dune Colossus | 51,0 | 29,7 |
| Verdant Singularity | 47,3 | 27,9 |
| Gravity Rebellion | 46,3 | 22,5 |
| Reality Ascendant | 53,6 | 26,3 |

[Maskinläsbara resultat](evidence/v0.3.0/benchmark.json) innehåller bildantal, medel- och högsta bildtid. Den tidigare okachade stagingflaskhalsen låg under en bild/s i 1080p och är korrigerad. Den nya versionen når fortfarande inte stabila 60 fps i RT vid standardupplösningen på denna GPU. För högre interaktiv bildfrekvens kan lägre upplösning, retrostil eller OpenGL-reservläget användas. **4K60-videoexport innebär inte 4K60 i realtid.**

## Linux och systemberoenden

Native ELF bygger och kontrollsviten passerar på Ubuntu 24.04. GPU-rendering i WSLg använder OpenGL via Microsoft D3D12 / NVIDIA GeForce RTX 4080, Mesa 25.2.8. Denna miljö erbjuder ingen verifierad Vulkan-RT-väg till NVIDIA-kortet. Mesa llvmpipe erbjuder ray queries i mjukvara och väljs därför bort av RT-backenden.

Linux GPU-tour renderade **152/152 bilder** via den automatiskt valda OpenGL-reserven. [Rörelseprovet](evidence/v0.3.0/motion-linux.json) godkände **76/76 kapitel**. Ett separat --require-rt-prov avvisade miljön med exitstatus 1, vilket bekräftar att llvmpipe inte räknas som hårdvaru-RT. Fysisk Linux-desktop med Vulkan-hårdvaruraytracing återstår att verifiera.

Linux smoke renderade även **32/32 scen/stil-bilder** via WSLg. Extraherade Windows-ZIP och Linux-tarball jämfördes mot aktuellt program och kombinerad WAV. Båda startades från annan arbetskatalog vid 42, 342 och 620 s, vilket täcker alla tre akter och resursvägar. Windows spelade ljud med icke-noll RMS i samtliga prov. Linux-paketprovet kördes uttryckligen utan ljud; dess ljudtransport täcks av CTest, inte detta displayprov.

ELF behöver libOpenGL.so.0, libstdc++.so.6, libm.so.6, libgcc_s.so.1, libc.so.6 och libGLdispatch.so.0. SDL laddar tillgängliga bild- och ljudbackends dynamiskt. Ubuntu-paket: libopengl0 libstdc++6 libx11-6 libxext6 libxrandr2 libxcursor1 libxi6 libxfixes3 libxss1 libwayland-client0 libxkbcommon0 libpulse0 libasound2t64, plus fungerande grafikdrivrutin. Systembibliotek ingår inte i tarballen.

## Musikregi och ljudidentitet

24 FFT-band mellan 35 Hz och 18 kHz och tre nivå-/anslagskurvor analyseras separat per akt vid 20 Hz. Kapitelregin använder faktisk anslagsstyrka och varaktig spektral förändring inom ±2,5 s från redaktionella mål. Dynamisk programmering håller kapitlen inom 5–18 s; inget fast BPM antas.

| Akt | Medelanslag vid ursprungliga mål | Vid valda klipp |
| --- | ---: | ---: |
| I | 0,1445 | 0,7749 |
| II | 0,2440 | 0,8584 |
| III | 0,1487 | 0,7852 |

[Alla klipp och förskjutningar](../music/direction.json) är spårbara. Detta stöder klippens anslagstajmning tekniskt; musikaliska fraser och upplevd synk behöver fortfarande bedömas genom lyssning.

Akt I:s första 299 s jämfördes sample för sample med bevarad full WAV. Därefter följer en sekunds likaeffektsuttoning. Den gemensamma WAV:en jämfördes bit för bit med de tre separata aktfilerna. Båda nya låtarna används i sin helhet, utan tillagda toningar eller hastighetsändring. Källa, Suno-ID, sampleantal och SHA-256 finns i music/asset-receipt*.json och music/show-audio.json.

## Videoexport och kvalitetsbedömning

Två deterministiska exportprov över aktgränserna passerade: 298–302 s gav exakt 240 bilder; 588,8–591,8 s gav 180 bilder. Båda är 3840×2160, 60 fps, H.264 NVENC, stereo AAC vid 48 kHz och korrekt ljudlängd. Strikt RT var aktiverat. Exportverktyget skyddar befintliga filer och verifierar metadata innan resultatet slutnamnges.

Den vertikala trailern passerade exportkontrollen: **1080×1920, 60 fps, 1 920 bilder, 32 s**, med stereo AAC och åtta ursprungliga native-klipp. De interna klippen exporteras med strikt RT; akt I behåller sin OpenGL-rendering. [Manifestet](evidence/v0.3.0/short.json) anger källprogram-/resurshash, bildtider, musikutsnitt och faktiska klipp. Trailern är ett montage av olika aktavsnitt; dess klipp ligger på anslag i ett 70–102 s-utsnitt ur Rigid Arpeggios. De ursprungliga animationerna i varje klipp reagerar på respektive aktmusik.

Den [oberoende granskaren](QUALITY_REVIEW.md) godkände treaktsimplementationen och Windows/Linux-paketen med **7,3/10**, över användarens acceptansgräns på 7/10. Byggnad, automation, GPU-bilder, musikidentitet och subjektiv helhetskvalitet är separata resultat. AAA-kvalitet, hörbar musikalisk likhet och subjektiv helhet är inte automatiskt verifierade av dessa prov. Fullängdsvideon och den färdiga Shortens bildresultat ingår inte i detta kod-/paketbetyg.
