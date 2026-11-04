# Pulse Ascendant v0.4 — oberoende variationsgranskning

Skrivskyddad granskning, 2026-10-12. Baseline `01a26a8aa1d2a3860148c8551080fe5ec549aeef`. Historisk rapport bevaras i [QUALITY_REVIEW-v0.3.0.md](QUALITY_REVIEW-v0.3.0.md).

## Granskarens slutrapport

Den granskade omarbetningen får **7,2/10** för formvariation, kapitelutveckling och läsbarhet. Betyget avser aktuell v0.4-källa och inspekterade nativebilder, oberoende av v0.3-betyget. Jag ser inga kvarvarande P1/P2-blockerare inom denna omfattning.

Granskningen omfattar diffen mot 01a26a8, samtliga världars tidiga/sena RT-tourbilder, Act II:s reservväg, v5-bilderna för City/Ocean och täta bildsekvenser kring kollisionen och finalen.

Konkreta fynd som nu är rättade och verifierade:

- **Wet Circuit:** den skymmande fasaden är borta ur kamerans siktlinje.
- **Act III 56/60/70:** trasiga SDF-sidor har ersatts av sammanhängande ytor.
- **Titan Overdrive 71:** kameran följer lyftet och behåller flottan synlig.
- **Fallback 39/43:** fransiga vågplattor och triangulerade tornsidor är rena i fullstora v5-bilder.
- **City/Ocean:** återkomsterna har nu tydligt olika huvudmotiv och kameravinklar.
- **Orbital Collision/finalen:** tidssekvenserna visar en enda kollision med efterföljande spridning respektive kristallupplösning, ren solhorisont och sluttoning.
- **Videoexport:** felet som kapade sista bilden vid vissa bildfrekvenser är identifierat och statiskt rättat; root rapporterar godkända regressionsprov.

Kvarvarande regikritik är främst Armadas täta magentafält, särskilt kapitel 12/22, och ett fortsatt enkelt material- och mönsterspråk i flera miljöer. Det begränsar bildhierarkin och finishen.

Låtarnas längder och kapitelgränser är bevarade. Akt II använder RT; akt I/III använder OpenGL. Lästa loggar visar Windows och Linux CTest **6/6**, samt v5-tour **152/152** för båda vägarna.

**Full kontinuerlig audiovisual upplevelse är inte godkänd genom denna granskning.** Tidsstickprov och källkod styrker förloppen, men bedömer inte hela filmens rytm, hörbara synk, kamerakomfort eller underhållningsvärde. Slutlig 4K-export och paketering återstår också utanför mitt betyg. Inget hinder från denna granskning mot att frysa nativeassets och genomföra exporten.

## Evidens

Native fångster: build/variation/tour-rt-v5 och tour-fallback-v5. Alla världstavlor finns under boards-rt-v5 / boards-fallback-v5. Rörelsestickprov: sequence-collision vid 4 bilder/s och sequence-finale vid 2 bilder/s ur den initiala 720p30-provfilmen. Exportens tidsavrundningsavvikelse redovisas separat i [VERIFICATION.md](VERIFICATION.md).

Den oberoende granskaren gjorde ingen mutation, egen GPU-körning eller kontinuerlig A/V-uppspelning. Senare artefakt-/publiceringsprov redovisas separat, inte som del av detta betyg.

## Uppföljning: uppstart och Short v2

Granskarens komplettering efter `faa0bf3`, fortfarande skrivskyddad:

> Runtimepatchen har nu inga blockerande fynd i den granskade källan. Uppvärmningen sker före ljudstart och båda uppspelningsklockorna. Den täcker även sena korridorens partikel-/spårprogram och Engine-programmets extra retropipeline. glFinish inväntar renderarbetet, aktuell fönsterstorlek återställs och Escape/quit kontrolleras före ljudstart. Mellanscenerna presenteras inte. Export, tour och selfTest går förbi uppvärmningen.
>
> Det lästa Linuxprovet visar 54,51 sekunders förberedelse, därefter 197 bilder på cirka 3 sekunder, med högsta bildtid 59,4 ms, vid showtid 342 s. Det stöder att förberedelsen flyttar den tidigare långa första bilden utanför spelklockan. Provet gäller en avgränsad OpenGL-körning och styrker inte jämn prestanda genom hela showen.
>
> Short v2:s Cityklipp är tydligt bättre än carrierklippet i vertikalt format: motiv, skala och rörelseutveckling går att läsa i de granskade bilderna. Thumbnailen är visuellt konsekvent med nativeunderlaget; 4K60-märkningen behöver stödjas av den färdiga exporten.
>
> Formvariationsbetyget ligger kvar på 7,2/10. Full kontinuerlig audiovisual upplevelse är fortsatt utanför godkännandet.
