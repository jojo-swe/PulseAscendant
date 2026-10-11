# Pulse Ascendant v0.3 — oberoende granskning

Granskare: `two_act_review`, skrivskyddad granskning 2026-10-11. Användarens godkännandegräns: minst 7/10. Följande är granskarens slutrapport; videoexportens slutresultat redovisas separat i VERIFICATION.md.

---

Jag godkänner den granskade treaktsimplementationen och Windows/Linux-paketen mot acceptansgränsen 7/10. **Samlat betyg: 7,3/10.** Inga kvarvarande blockerande fynd i demot.

| Område | Betyg |
| --- | ---: |
| Kod och robusthet | 8/10 |
| Grafik i inspekterade bilder | 7,2/10 |
| Musikalisk teknik och klippning | 7,5/10 |
| Interaktiv RT-prestanda | 6/10 |
| Verifierbarhet | 8/10 |

Tre akter, 16 världar och 76 kapitel är integrerade över exakt 888,68 sekunder. Jag verifierade ljudhashar, frameantal och att den gemensamma WAV-filen innehåller aktfilerna sampleidentiskt. Akt II och III använder hela respektive låt. Klippregin söker faktiskt uppmätta anslag och spektrala förändringar.

Vulkan-koden använder riktiga triangel-BLAS/TLAS och ray queries. Stagingöverföring, barriärer, fence, storlekskontroll och resursstädning gav inga blockerande fynd. CPU-enheter avvisas nu; färska Linuxprov bekräftar automatisk OpenGL-reserv och avvisning med strikt RT-krav. Windowsfilen har ingen obligatorisk Vulkan-DLL-import.

Jag granskade representativa bilder från samtliga världar och flera aktuella akt III-kapitel. De tidigare kamerablockeringarna är åtgärdade. Världarna är tydligt olika, med läsbara huvudmotiv och flera samtidiga lager. Bildpolishen varierar: vegetation och vissa konstruktioner är enkla, geometrin upprepas tydligt och tunna detaljer visar aliasing. Fallbackbilden `chapter-56-a.bmp` har synliga randartefakter på vissa plattformskanter.

RT-mätningarna visar **37–54 fps i 720p och 19–30 fps i 1080p på RTX 4080**. Den allvarliga tidigare flaskhalsen är löst, men stabila 60 fps vid standardupplösningen är inte uppnådda.

Lästa slutloggar visar Windows/Linux CTest 6/6, strikt RT-smoke 32/32 och tour 152/152. RT, Windows-fallback och Linux-fallback har vardera 76/76 godkända rörelsekontroller. Jag kontrollerade paketens innehåll; Windowsprogrammets och shadernas hashar matchar aktuell byggnad. Extraherade paketprov bekräftar start i alla tre akter från annan arbetskatalog.

Short-verktygets statiska kontroll passerade för åtta klipp och totalt 1 920 bilder. Ett mindre återkörningsfel kvarstår: dess klippcache saknar koppling till program-/resurshashar och kan återanvända äldre material.

Granskningen var skrivskyddad, utan egna GPU-körningar. Obruten lyssning, upplevd synk, hela rörelsedramaturgin och fysisk Linux-RT återstår att bedöma. Fullängdsvideon och Shortens färdiga bildresultat ingår ännu inte i godkännandet. **AAA-kvalitet är inte verifierad.**

## Granskarens tillägg efter korrigering

Tillägg till slutrapporten: Short-cachefelet är åtgärdat. Cachemappen identifieras nu av SHA-256 över renderkonfiguration, programfil och samtliga programresurser; samma hash sparas i manifestet.

RT-texten visas inom klippgränserna för Rain District och Prism Chamber, som exporteras med strikt RT-krav. Python-syntaxkontrollen passerade, och ett ljudprov med ffmpeg accepterade den korrigerade toningen `d=0.35`.

Inga nya blockerande fynd. **Slutbetyget står fast på 7,3/10.** Shortens färdiga bild-, ljud- och klippupplevelse återstår att granska. Inga filer ändrades och inga GPU-jobb kördes.

## Publiceringsmaterial och trailerrevision 2

Följande är samma granskares separata bild-/metadatagranskning. Det ändrar inte det tidigare kod-/paketbetygets omfattning.

Publiceringstexten och thumbnailen är godkända inom granskad omfattning. Titeln är ärlig, sakpåståendena stämmer med dokumentationen och thumbnailens motiv överensstämmer visuellt med Chrome-fångsten. Huvudtexten och ”4K60” är läsbara vid 320×180.

Shorten har korrekt metadata: **1080×1920, 60 fps, 1 920 bilder, 32 sekunder och stereo AAC**. Hela videoströmmen avkodades utan fel. Jag inspekterade åtta representativa stillbilder samt fyra extra från stadsklippet.

Jag rekommenderar omklippning före fortsatt marknadsföring: **Rain District vid 8,15–12,25 s är klart svagast**, med mörka fasader som skymmer motivet. Använd helst sju befintliga klipp fördelade över 32 sekunder. RT-texten passar över Prism och Dune.

Små korrigeringar: ange endast Rigid Arpeggios i Shortens musiktagg och använd 9:50 för huvudvideons akt III-kapitel.

Fullvideons 4K60-påstående förutsätter godkänd slutex­port. Jag har inte bedömt ljudet genom lyssning eller kontinuerlig rörelseupplevelse. Inga webbplatser ändrades och inga GPU-jobb kördes.

### Slutkontroll efter omklippning

**Korrigerad V2 är godkänd inom granskad omfattning.**

Bilden vid 10 s visar Prism Chamber med läsbar RT-text. Manifestet använder `354.95`; Rain District är borttaget. RT-textens intervall **9,45–18,15 s** täcker Prism+Dune korrekt.

Det tidigare hindret är åtgärdat. Övriga passerade kontroller gäller fortsatt. Godkännandet omfattar metadata och inspekterade bilder; ljudupplevelse och fullängdsvideo har inte bedömts. Inga GPU-jobb kördes.
