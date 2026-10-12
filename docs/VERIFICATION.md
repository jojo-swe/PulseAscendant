# Pulse Ascendant v0.4 — verifiering

Denna version omarbetar regi, geometri och kameror efter kritik om upprepning. Historisk evidens finns i [VERIFICATION-v0.3.0.md](VERIFICATION-v0.3.0.md). Dess rörelseprov, prestandamätningar och video gäller inte automatiskt v0.4.

## Omfattning och musik

Tre akter, 16 världar och 76 kapitel över **888,68 sekunder**. Akt I använder 300 s Luminous Flow, akt II hela Adventurous Uplift och akt III hela Rigid Arpeggios. Musik, hastighet och kapitelgränser är bevarade.

| Akt | PCM-stereoframes vid 48 kHz | Start | Längd | Kapitel |
| --- | ---: | ---: | ---: | ---: |
| I | 14 400 000 | 0 s | 300 s | 31 |
| II | 13 916 160 | 300 s | 289,92 s | 21 |
| III | 14 340 480 | 589,92 s | 298,76 s | 24 |

tests/test_show.py passerar: första 299 s är sampleidentiska med bevarad original-WAV, därefter följer akt I:s tidigare uttoningssekund. Den sammanfogade filen är sampleidentisk med aktfilerna vid båda skarvarna. Källhashar, aktlängder och nekad export förbi slutet är kontrollerade. Runtime-WAV har SHA-256 `58ca253cb4c427c0aa89f96e9cd4e12efb02ee8852ea75fd2cac0159321b2ea6`.

## Native byggnad och kontrollprov

- Windows x64 Release, MSVC/SDL3: **CTest 6/6 passerar**.
- Linux x64 Release, Ubuntu 24.04/GCC/SDL3: **CTest 6/6 passerar** på isolerad Xvfb/Openbox med Mesa llvmpipe.
- Python: **5/5 FFT-/musikregiprov passerar**, samt det separata fullständiga ljud-/showprovet.
- Vulkan-computeshadern kompilerad med glslangValidator för Vulkan 1.2 och kontrollerad med spirv-val.

Kärnproven täcker nu kronologiska världstillstånd, stabil kamera inom kapitlet, kontinuerlig rörelseklocka över klipp, monotont kapitel-/handlingsförlopp samt anslagets eventmaximum. Den tidigare fyrkamera-cykeln är borttagen. Kontrollproven täcker sökning, stil, mute, observerad fullscreen/resize/minimize/restore, fortsatt klocka vid minimering och avbrott som måste ge felstatus.

Linux mjukvarurenderare behövde 37–40 s för shaderstart och kontrollprov; den tidigare yttre 30 s-gränsen gav två timeouts. Startallowansen är nu 180 s, medan själva livscykelns 15 s-gräns och felkrav är oförändrade. X11-sockets isolerades i en privat mount-namespace; WSLg:s display ändrades inte.

[Windowslogg](evidence/v0.4.0/windows-tests.log), [Linuxlogg](evidence/v0.4.0/linux-tests.log), [show-info](evidence/v0.4.0/show-info.json) och [native-/resurshashar](evidence/v0.4.0/native-assets.json).

### Förberedelse före musikstart

Linux drivrutin kunde tidigare blockera första bilden i 35,74 s medan musikens klocka redan gick. Interaktiv uppspelning förbereder nu samtliga världars grafikprogram, sena korridorens partikel-/spårprogram, Engine-programmets retropipeline och startbildens fulla viewport **före ljudstart och båda uppspelningsklockorna**. Fönstertiteln visar förberedelsens framsteg; Escape/quit avbryter. Export, tour och kontrolltest använder sina tidigare separata körvägar.

Slutversionens Windowsprov vid 342 s: 0,119 s förberedelse, sedan 301 bilder på cirka 3 s, ljud-RMS 0,149 och ljudklocka 342–345 s. Slutversionens WSLg-prov: 67,67 s förberedelse, sedan 261 bilder på cirka 3 s och högsta bildtid 20,23 ms. Dessa körningar gjordes vid 640×360 **samtidigt som 4K-exporten körde**; de verifierar ordning och avgränsad uppspelning, inte fristående prestanda eller hela showens flyt. Linuxprovet använde den tysta klockan.

Efter denna runtimeändring passerar Windows CTest åter **6/6** (10,92 s) och Linux CTest **6/6** (108,21 s). [Windows uppspelning](evidence/v0.4.0/windows-prepared-run.log), [Linux uppspelning](evidence/v0.4.0/linux-prepared-run.log). Den visuella källan är fryst vid `faa0bf3b4a96dc4b380f822462024ee54a7eb9e2`; medieexportens producent skiljer sig från paketens senare runtime endast genom denna uppstartsändring. Grafikresurserna är identiska.

## GPU och bildgranskning

Windows: NVIDIA GeForce RTX 4080, drivrutin 617.42. OpenGL 3.3 och Vulkan KHR ray queries.

- Strikt RT med Vulkan-validering: **152/152 kapitelbilder**, inga rapporterade GL-/Vulkanfel; **42 RT-bilder**, samtliga i akt II.
- Tvingad OpenGL-reserv: **152/152 kapitelbilder**, inga rapporterade GL-fel.
- Varje par visar 18% och 82% av kapitlet, före och efter dess utveckling. Nativebilder och världstavlor granskades över alla 76 kapitel. Täta tidssekvenser kontrollerades dessutom kring kollisionen och finalen.

[RT-logg](evidence/v0.4.0/tour-rt.log), [fallbacklogg](evidence/v0.4.0/tour-fallback.log). Bildprov visar renderbarhet, bildinnehåll och inspekterade förlopp; de bevisar inte obruten underhållningskvalitet.

**Akt II använder hårdvaruraytracing. Akt I och III använder OpenGL.** Ray queries driver primärsynlighet, skuggor, reflektion och brytning över triangel-BLAS/TLAS. Materialmodellen och analytisk miljöbelysning är förenklade; generell GI och temporal denoising saknas. Detta är ingen unbiased path tracer. Reservläget har enklare raymarchad geometri/material och lovar inte identisk bild.

4K60 är deterministisk videoexport, inte ett påstående om interaktiv 4K60. Tidigare v0.3-fpsvärden återanvänds inte som aktuell benchmark. Fysisk Linux-desktop med Vulkan-RT är inte verifierad; WSLg använder OpenGL via D3D12/NVIDIA.

## Oberoende granskning och gränser

Den nya skrivskyddade granskaren ger **7,2/10 för formvariation, kapitelutveckling och läsbarhet**. Inga kvarvarande P1/P2-blockerare inom granskad nativeomfattning. Kameraskymning, felaktiga SDF-sidor, förlorad flottinramning och två fallbackytfel är rättade och bildverifierade. [Full rapport](QUALITY_REVIEW.md).

Armadas täta magentafält och det enklare material-/mönsterspråket begränsar fortfarande finishen. Ingen obruten mänsklig lyssning eller full kontinuerlig audiovisual bedömning av hela demot är godkänd. Betyget är därför inte ett godkännande av underhållningsvärde eller AAA-kvalitet.

## Videoexport

Exportören skriver exakt ceil(duration × fps) nativebilder och begränsar ljudet separat. Den tidigare globala ffmpeg-tidsgränsen kapade en bild i 720p30-provfilmen; den initiala filmen används endast för tidsstickprov och räknas inte som godkänd slutexport.

Regressioner: 0,07 s vid 30 fps ger **3 bilder**, och 0,035 s vid 3840×2160/60 fps ger **3 bilder**, med korrekt ljudlängd. [30 fps metadata](evidence/v0.4.0/frame-rounding-30.json), [4K60 metadata](evidence/v0.4.0/frame-rounding-4k60.json). Exportören skyddar befintliga filer, kontrollerar native show-info och WAV-format/sampleantal samt slutnamnger endast efter metadata-PASS.

Fullständig v0.4-video renderas fortfarande; historiska v0.3-filer räknas inte som nya resultat. Exportens käll-/producenthash är dokumenterad separat från den senare interaktiva uppstartsändringen.

## Paket och publicering

[Offentlig v0.4.0-release](https://github.com/jojo-swe/PulseAscendant/releases/tag/v0.4.0), exakt käll-/byggcommit `cd67f402b8d623895bea2dafcb18fdc457987d97`. Windows- och Linuxpaketen startade från orelaterad arbetskatalog vid 42, 342 och 620 s efter förberedelse. Windows använde verklig ljuduppspelning med observerad RMS; Linux använde tyst reservklocka på WSLg.

ZIP-filers CRC kontrollerades för alla poster. Källarkivet jämfördes med `git archive` av exakt byggcommit, inklusive exportens radslut, samt med alla tre original-MP3 och fyra WAV. Paketens nativebinärer, samtliga shader-/musikhashar och Linux körbehörighet verifierades. Alla åtta GitHub-assets har samma SHA-256 och storlek som lokala slutartefakter. [Paketreceipt](evidence/v0.4.0/release-receipt.json).

[Nya offentliga trailern](https://youtube.com/shorts/uOhjmoLY6DI) är revision 4: 1080×1920, 60 fps, 1 920 bilder, 32 s och sju nativeklipp. Full GPU-assisterad videodekodning passerar. RT-rubriken visas enbart över akt II-klipp. Bildgranskning täcker motivens läsbarhet i vertikalt format; den garanterar inte viralitet. YouTube visar sparad offentlig synlighet, färdig HD-bearbetning och inga hittade upphovsrättsproblem vid uppladdningen. Suno-musiken är deklarerad i beskrivningen och AI-inställningen.

Den nya fullvideons YouTube-publicering och ändringen av gamla fullvideon till olistad återstår tills slutexporten har verifierats. Gamla versioners publiceringsbevis återanvänds inte för dessa steg.
