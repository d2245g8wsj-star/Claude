# Nazaré — referentiemetingen Slate VCC (Channel en MixBuss)

*Gemeten op 19 november 2026, Apple Silicon, macOS 15.6, 48 kHz, offline via pedalboard 0.9.26 (AU).*
*Black-box: testsignaal erin, audio eruit. Er is niets gedecompileerd of gekopieerd.*

Dit verslag zet de metingen om in **ontwerpregels en kalibratiedoelen** voor de zes modellen van Nazaré. Alle ruwe data staan in `results/` (zie §9).

---

## 1. De tien belangrijkste lessen voor Nazaré

1. **VCC is geheugenloos.** Na een sprong van −30 naar −6 dBFS (en terug) liggen gain, H2 en H3 binnen één blok van 20 ms op hun eindwaarde. Er is geen meetbare dynamische bias, envelope of hysterese. Het karakter komt dus volledig uit *statische* waveshapers met filters eromheen. **Kans voor Nazaré:** een langzame bias of een "sag" is een echt onderscheidend kenmerk.
2. **Links en rechts zijn bit-identiek.** Er zit geen componentspreiding in: gain, H2 en H3 verschillen 0,000 dB tussen L en R. **Kans:** een spreiding van ±0,05–0,2 dB in gain en ±1–3 dB in H2 per kanaal maakt het stereobeeld breder en "echter".
3. **Geen oversampling, 0 samples latency.** Een toon van 9973 Hz op 0 dBFS (drive 18) geeft een gespiegelde H3 op 18 081 Hz. Die ligt bij console 5 op −27 dB en bij console 2 op −37 dB. **Nazaré moet oversamplen** (minstens 4× bij de zware modellen) en kan dit als kwaliteitsvoordeel communiceren. Als er een zero-latency-modus komt, dan alleen als optie.
4. **Ruis bestaat alleen zolang er signaal loopt.** Bij digitale stilte is de uitgang exact 0, of ruisonderdrukking nu aan of uit staat. Met een toon van 1 kHz op −18 dBFS ligt de ruis rond −93 tot −107 dBFS (NR uit) of −100 tot −131 dBFS (NR aan). Het is dus een signaalgestuurde ruispoort. Voor Nazaré: ruis gaten met een snelle envelope, zodat stiltes in de DAW stil blijven.
5. **Twee soorten "H2-kleur".** Consoles 0 en 1 (Channel) geven een H2 die vrijwel **niet** van het niveau afhangt: −60 dB van −36 tot 0 dBFS. Daarbij hoort een vaste even reeks: H4 is precies 14 dB zachter, met daarna H6, H8 en H10. Dat is de handtekening van `y = x + k·|x|`, oftewel een iets andere versterking voor de positieve en de negatieve halve golf, met een hoogdoorlaat om de DC weg te halen. Je krijgt dus *altijd* wat kleur, ook bij zachte signalen. De andere consoles gedragen zich als echte polynomen, waarbij de vervorming oploopt met het niveau.
6. **Drive is "meer asymmetrie", geen "meer ingangsniveau".** Bij consoles 0, 1 en 4 stijgt THD vrijwel dB-voor-dB met de drive: +18 dB drive geeft ongeveer +17 dB THD. Het knikpunt verschuift daarbij niet en de gain stijgt maar 0,1–1,2 dB. Een makeup-gain is daar dus nauwelijks nodig. Alleen console 5 (en 3 op MixBuss) gedraagt zich als een echte gain-in-shaper.
7. **Een harde grens net boven 0 dBFS.** Bij de meeste consoles springt THD tussen 0 en +3 dBFS van ongeveer −50 naar ongeveer −20 dB. Console 2 Channel knikt al bij 0 dBFS. Consoles 3 en 4 Channel clippen bij +3 dBFS nog niet. Nazaré moet per model kiezen: een harde ceiling of headroom.
8. **Frequentie-afhankelijke vervorming is het "geheim".** Console 4 geeft bij 100 Hz −101 dB THD, bij 1 kHz −80 dB en bij 10 kHz −77 dB: de vervorming neemt toe met de frequentie, zoals bij slew of een HF-pre-emphasis voor de shaper. Console 3 doet het omgekeerde: veel H3 bij 100 Hz (−66 dB) en weinig bij 1 kHz (−84 dB). Dat is het klassieke beeld van een transformator met kernverzadiging. **Dit zijn de twee goedkoopste "echtheids"-trucs:** een shelf vóór de shaper, met het omgekeerde filter erna.
9. **MixBuss = Channel plus overspraak en een iets andere curve.** Channel heeft geen meetbare overspraak (−234 dB). MixBuss wel: tussen −41 en −120 dB, **frequentie-afhankelijk en per console anders** (§6). De nonlineariteit van consoles 4 en 5 is in beide plugins vrijwel gelijk.
10. **De frequentierespons verandert nauwelijks met het niveau.** Op −30, −18 en −6 dBFS liggen de curves binnen 0,05 dB van elkaar. De enige uitzondering is console 2 Channel: onder 50 Hz gaat de respons van +0,25 dB naar −0,39 dB. Dat is een **niveau-afhankelijke LF**, zoals bij een transformator.

---

## 2. Opzet en beperkingen

| Onderdeel | Waarde |
|---|---|
| Plugins | VM VCC Channel, VM VCC MixBuss (AU, universal binary) |
| Parameters | `console` 0–5 (6 stappen), `drive` 0–18 dB (standaard 6), `input`/`output` ±18 dB (op 0), `noisereduction` aan/uit |
| Drive-standen | 0 / 4,5 / 6 / 9 / 13,5 / 18 dB → 72 configuraties |
| Niveaus | −42 tot +3 dBFS in stappen van 3 dB (sinus, piekniveau) |
| Analyse | FFT van 1 s (bins van 1 Hz, exacte bins), Hann-venster |

**Beperkingen**
- **Consolenamen:** de plugin geeft alleen de nummers 0–5 door, geen namen. Vul in §3 de namen in vanuit het keuzemenu in de plugin (dezelfde volgorde).
- **Waves NLS is niet gemeten.** WaveShell2-AU 12.0 blijft hangen bij het laden (na 150 s nog geen reactie). In WaveShell 12.1 zit NLS niet. Waarschijnlijke oorzaak: een licentiedialoog of een oude shell op macOS 15. Zie §10.
- De meting is offline gedaan. Als VCC in realtime een andere (bijvoorbeeld overgesamplede) modus heeft, zien we die hier niet.
- Harmonischen boven Nyquist (bij 10 kHz alles vanaf H3) zijn niet direct gemeten; zie de aliasingmeting.

---

## 3. Profiel per console (drive 6 dB, de standaardstand)

> Getallen: THD en harmonischen in dB ten opzichte van de grondtoon. "Gain" is de insertiegain bij 1 kHz.
> Vul de naam per console in vanuit het VCC-menu: 0 = …, 1 = …, 2 = …, 3 = …, 4 = …, 5 = …

### Console 0: "asymmetrische even kleur, met lichte groei"
- **Channel:** H2-dominant. THD −58 / −54 / −50 dB bij −18 / −6 / 0 dBFS. Gelijk bij 100 Hz, 1 kHz en 10 kHz (frequentie-onafhankelijk). H4 ligt constant op −74 dB, dus er is een `|x|`-component plus een kwadratisch deel. Clipt bij +3 dBFS (−27 dB).
- **MixBuss:** heel anders. Hier domineert H3 (H2 −75, H3 −64 bij −6 dBFS) en de THD-curve is **niet monotoon**: −72 bij −36, piek −54 bij −18, terug naar −62 bij −6. IMD *daalt* met het niveau (−47 → −54). Dit wijst op een shaper met een "bult" rond −20 dBFS (crossover-achtig of bias-verschoven).
- **FR:** de duidelijkste "warme" tilt. Channel: −0,97 dB @20 Hz, −0,35 @10 k, −0,55 @20 k. MixBuss: −1,26 @20, een kleine dip van −0,42 rond 200 Hz, en −0,83 @20 k.
- **Gain:** +0,63 dB (Channel), +0,94 dB (MixBuss). Het meest "luider = beter" van alle consoles.

### Console 1: "vaste H2-glans"
- **Channel:** H2 **−60 dB, ongeacht het niveau** (−36 tot 0 dBFS), H4 −74 dB, H3 stijgt pas boven −6 dBFS. IMD ligt vlak op −53 dB. Puur `x + k·|x|` met k ≈ 0,0024 bij drive 6 (zie §7). Clipt bij +3 dBFS.
- **MixBuss:** wel niveau-afhankelijk (THD −96 bij −36 → −56 bij 0), met H2 ≈ H3.
- **FR:** licht. −0,64 @20 Hz, en hoog −0,24 (Channel) of −0,65 (MixBuss) @22 k.
- **Gain:** +0,27 dB.

### Console 2: "schoon tot de muur"
- **Channel:** bij 1 kHz heel schoon (−82 bij −18), maar bij 0 dBFS een **harde knik** naar −41 dB, en bij +3 −17 dB met −1,5 dB gain. Bij 100 Hz domineert H3 al vanaf −18 dBFS (−63 dB): LF-afhankelijk. De FR heeft een "smiley met dip": −0,4 dB rond 100–200 Hz, +0,27 @5 kHz, en LF die met het niveau verschuift.
- **MixBuss:** een **zuiver oneven reeks** (H3, H5, H7, H9; de even harmonischen liggen onder −140 dB). Dat is een symmetrische soft clip. THD stijgt tot een plateau van ongeveer −72 dB vanaf −18 dBFS, met een harde clip bij +3. De FR is boven 500 Hz exact vlak; alleen een hoogdoorlaat (−1,33 @20 Hz).
- **Aliasing (drive 18):** −34 dB op Channel. De harde knik vraagt veel oversampling.

### Console 3: "transformator-laag"
- **Channel:** de **grootste LF-boost** van allemaal: +5,2 dB @20 Hz, +3,9 @31, +2,3 @50, +1,1 @80, +0,7 @100. Bij 100 Hz stijgt H3 snel (−93 → −61 dB van −36 naar 0 dBFS); bij 1 kHz blijft het schoon (−84). De gain *daalt* licht met het niveau (+0,18 → +0,07): een zachte compressie. Clipt bij +3 dBFS niet.
- **MixBuss:** geen LF-boost (−0,66 @20). Wel H3 bij laag (−69 bij 100 Hz, −18 dBFS). Drive gedraagt zich hier wél als ingangsgain: bij drive 18 is de kleinsignaalgain +3,05 dB en bij 0 dBFS slaat de THD om naar −26 dB.
- **Aliasing:** de laagste van alle consoles (−84 dB, Channel).

### Console 4: "HF-glans, lage-niveau-ruwheid"
- **Channel en MixBuss (vrijwel gelijk):** de vervorming hangt sterk af van de **frequentie**: −105 dB @100 Hz, −77 @1 kHz, −77 @10 kHz (alleen H2). Bij 1 kHz is het **relatief vuiler bij zachte signalen** (THD −71 bij −36 dBFS, −80 bij −6 dBFS). Dat is crossover-achtig, of een vaste absolute vervormingscomponent. Bij +3 dBFS geen clip.
- **FR:** een **HF-shelf**: +0,19 @2 k, +0,36 @5 k, +0,46 @10 k, +0,57 @20 k. LF −0,48 @20 Hz (Channel).
- **Drive:** de gain *daalt* licht (+0,18 → +0,05 dB) terwijl de THD stijgt (−83 → −65 @−18 dBFS).

### Console 5: "buis: dik, comprimerend, asymmetrisch"
- **Channel en MixBuss (zelfde nonlineariteit):** de zwaarste. THD −48 / −32 / −25 dB bij −18 / −6 / 0 dBFS. **H2 ≈ H3** (−34 / −36 bij −6 dBFS), met een lange staart (H4 −49, H5 −54, H6 −58). **Compressie:** −1,4 dB bij 0 dBFS (Channel, drive 6), tot −2,0 dB bij drive 13,5. **Asymmetrische pieken:** + 0,439 en − 0,460 bij 100 Hz −6 dBFS. IMD −38 / −23 / −17 dB.
- **Drive:** werkt als ingangsgain met compensatie. Bij 0 dBFS gaat THD van −57 (d0) naar −28 (d4,5) naar −18 dB (d18). De kleinsignaalgain stijgt (−0,58 → +1,19 dB) terwijl de grootsignaalgain daalt. Het effect is netto een **verdichting van ongeveer 3 dB**.
- **FR:** lichte HF-lift (+0,6 @20 k) met een kleine dip rond 8 kHz.
- **Overspraak (MixBuss):** de sterkste, −41 dB.
- **Aliasing (drive 18):** −27 dB. Deze console *moet* in Nazaré overgesampled worden.

---

## 4. Vergelijkingstabellen (drive 6 dB)

### 4.1 THD bij 1 kHz (dB) en gain

| Console | Ch −18 | Ch −6 | Ch 0 | Ch +3 | MB −18 | MB −6 | MB 0 | MB +3 | Gain Ch / MB |
|---|---|---|---|---|---|---|---|---|---|
| 0 | −58 | −54 | −50 | −27 | −54 | −62 | −63 | −23 | +0,63 / +0,94 |
| 1 | −60 | −60 | −60 | −30 | −70 | −63 | −56 | −27 | +0,27 / +0,29 |
| 2 | −82 | −73 | −41 | −17 | −71 | −72 | −72 | −19 | +0,43 / +0,22 |
| 3 | −90 | −84 | −83 | −80 | −82 | −79 | −75 | −24 | +0,16 / +0,15 |
| 4 | −77 | −80 | −74 | −69 | −77 | −80 | −75 | −70 | +0,16 / +0,17 |
| 5 | −48 | −32 | −25 | −23 | −48 | −32 | −25 | −23 | −0,31 / +0,11 |

### 4.2 Harmonische handtekening (1 kHz, −6 dBFS, dB t.o.v. grondtoon)

| Console | H2 | H3 | H4 | H5 | H6 | H7 | Karakter |
|---|---|---|---|---|---|---|---|
| Ch 0 | −54 | −77 | −74 | −100 | −81 | −110 | even reeks |
| Ch 1 | −60 | −94 | −74 | −101 | −81 | −110 | zuiver `|x|`-reeks |
| Ch 2 | −73 | −82 | −120 | −99 | −127 | −107 | H2 + oneven |
| Ch 3 | −89 | −87 | −112 | −107 | −119 | −120 | gebalanceerd, laag |
| Ch 4 | −82 | −85 | −94 | −96 | −99 | −99 | breed, vlak verval |
| Ch 5 | −34 | −36 | −49 | −54 | −58 | −83 | buis, dikke staart |
| MB 0 | −75 | −64 | −73 | −70 | −74 | −75 | oneven, vlak verval |
| MB 2 | −80 | −73 | −144 | −82 | −147 | −90 | zuiver oneven |

### 4.3 Frequentierespons (−18 dBFS, dB t.o.v. 1 kHz)

| Console | 20 Hz | 50 | 100 | 200 | 2 k | 5 k | 10 k | 20 k |
|---|---|---|---|---|---|---|---|---|
| Ch 0 | −0,97 | −0,27 | −0,07 | −0,15 | 0,00 | −0,14 | −0,35 | −0,55 |
| Ch 1 | −0,64 | −0,11 | −0,03 | −0,01 | 0,00 | −0,03 | −0,10 | −0,22 |
| Ch 2 | 0,00 | −0,09 | −0,37 | −0,39 | +0,17 | +0,27 | +0,23 | +0,02 |
| Ch 3 | **+5,18** | +2,27 | +0,73 | +0,17 | +0,03 | +0,12 | +0,20 | +0,29 |
| Ch 4 | −0,48 | −0,04 | +0,02 | +0,03 | +0,19 | +0,36 | +0,46 | +0,56 |
| Ch 5 | −0,26 | +0,11 | +0,09 | +0,02 | +0,21 | +0,27 | +0,21 | +0,60 |
| MB 0 | −1,26 | −0,66 | −0,32 | −0,42 | +0,04 | −0,04 | −0,32 | −0,83 |
| MB 2 | −1,33 | −0,24 | −0,06 | −0,01 | 0,00 | 0,00 | 0,00 | 0,00 |
| MB 3 | −0,66 | −0,12 | −0,04 | −0,02 | +0,03 | +0,12 | +0,18 | +0,24 |

Alle curves zijn klein (meestal < 1 dB) en zacht. **Vertaling:** één of twee shelves plus een hoogdoorlaat van de eerste of tweede orde per model zijn genoeg. Dure EQ-modellen zijn niet nodig.

### 4.4 IMD (SMPTE 60 Hz + 7 kHz, 4:1)

| Console | −18 dBFS | −6 | 0 |
|---|---|---|---|
| Ch 0 | −51 | −47 | −43 |
| Ch 1 | −53 | −53 | −53 |
| Ch 2 | −76 | −66 | −43 |
| Ch 3 | −67 | −59 | −64 |
| Ch 4 | −72 | −71 | −64 |
| Ch 5 | −38 | −23 | −17 |
| MB 0 | −47 | −52 | −54 |

Let op console 3: IMD bij 0 dBFS is *lager* dan bij −6 dBFS. De LF-verzadiging moduleert de 7 kHz dus niet monotoon.

---

## 5. Drive-gedrag (1 kHz)

| Console | Gain −18: d0 → d18 | THD −18: d0 → d18 | THD 0 dBFS: d0 → d18 | Type drive |
|---|---|---|---|---|
| Ch 0 | +0,45 → +1,65 | −64 → −47 | −56 → −39 | asymmetrie-schaal |
| Ch 1 | +0,08 → +1,28 | −66 → −49 | −65 → −49 | asymmetrie-schaal |
| Ch 2 | +0,42 → +0,45 | −88 → −70 | −41 → −40 | kleur-schaal, vaste muur |
| Ch 3 | +0,07 → +0,70 | −96 → −78 | −88 → −72 | schaal |
| Ch 4 | +0,18 → +0,05 | −83 → −65 | −80 → −62 | schaal, gain daalt |
| Ch 5 | −0,58 → +1,19 | −54 → −37 | −57 → −18 | **ingangsgain + compressie** |
| MB 3 | −0,45 → +3,05 | −87 → −72 | −80 → −26 | ingangsgain |
| MB 5 | −0,17 → +1,61 | −54 → −37 | −57 → −18 | ingangsgain + compressie |

**Ontwerpregel:** bij vijf van de zes consoles schaalt `drive` de *vervormingscoëfficiënt* met ongeveer 1 dB THD per dB drive, zonder het clippunt te verplaatsen. Voor Nazaré: `k_eff = k_model · 10^(drive_dB/20)` voor de kleurterm, met het clipdeel apart. Alleen het buismodel krijgt "echte" pre-gain met een (gedeeltelijke) post-compensatie.

---

## 6. Overspraak (alleen MixBuss)

L→R gelijk aan R→L. Lineair: hetzelfde bij −18 en −6 dBFS, behalve bij console 2.

| Console | 100 Hz | 1 kHz | 10 kHz | Vorm |
|---|---|---|---|---|
| 0 | −76 | −75 | −77 | vlak |
| 1 | −76 | −74 | −77 | vlak |
| 2 | −95 | −120 | −101 | U-vorm, laag niveau |
| 3 | −65 | −70 | −75 | **LF-gedomineerd** (inductief/gemeenschappelijke aarde) |
| 4 | −85 | −84 | −71 | **HF-gedomineerd** (capacitief) |
| 5 | −41 | −50 | −42 | U-vorm, sterk |

In het andere kanaal lekt bijna geen H2 mee (−79 tot −137 dB). De overspraak zit dus vóór of naast de shaper, en is lineair.
**Vertaling:** een 2×2-matrix met per model één filter in de overspraakterm (vlak, laagdoorlaat, hoogdoorlaat of band-stop). Goedkoop en goed hoorbaar in het stereobeeld.

---

## 7. Modelvoorstellen (startwaarden voor de DSP)

Dit zijn **startpunten** om tegen `summary.csv` te fitten, geen kopieën.

```
Nazaré-kanaal = HPF → pre-shelf → shaper(drive) → [clip] → post-shelf (inverse pre) → tilt-EQ → crosstalk-matrix → noise-gate-ruis
```

| Bouwsteen | Ontwerp | Doel uit de meting |
|---|---|---|
| Asymmetrische kleur ("ref 0/1") | `y = x + k·|x|`, met daarna DC-HPF (~5–10 Hz) | H2 −60 dB vlak, H4 = H2 − 14 dB. k ≈ 0,0024 @drive 6 (Ch 1), ×10^(Δdrive/20) |
| Polynomiale groei ("ref 0") | `+ a2·x²` bovenop het bovenstaande | H2 van −60 → −50 tussen −36 en 0 dBFS |
| Harde muur ("ref 2") | soft-knee clip, knie bij ≈ 0 dBFS, verder lineair | THD −73 @−6 → −41 @0 → −17 @+3 |
| Symmetrische soft clip ("ref MB 2") | `tanh`-achtig, alleen oneven | plateau ≈ −72 dB vanaf −18 dBFS |
| Transformator-LF ("ref 3") | LF-shelf +5 dB @20 Hz plus een shaper die alleen LF ziet (bandsplit < 200 Hz) | H3 @100 Hz −75 / −66 / −61; @1 k < −84 |
| HF-afhankelijke kleur ("ref 4") | HF-pre-emphasis (+ ≈ 25 dB per decade boven ~300 Hz) → zachte shaper → de-emphasis | THD 100 Hz −105, 1 k −77, 10 k −77 |
| Buis ("ref 5") | asymmetrische tanh (bias-offset) met pre-gain en makeup, plus een lange staart | H2 ≈ H3 ≈ −35 @−6, compressie −1,4 dB @0, pieken +0,44 / −0,46 |
| Ruis | ruis van ≈ −95 dBFS, gegated door de signaalenvelope (snelle attack, release ~50 ms); NR-modus −20 dB | 0 bij stilte, −93…−107 dBFS met signaal |
| Overspraak | `R += h_xt * L` (en andersom), filter per model | tabel §6 |

**Waar Nazaré beter kan dan VCC (onderscheidend):**
1. Oversampling: VCC laat aliases tot −27 dB toe.
2. L/R-componentspreiding: VCC is bit-identiek.
3. Dynamisch geheugen of bias-drift (VCC is volledig statisch). Bijvoorbeeld een bias die met een tijdconstante van 50–300 ms de envelope volgt, zodat H2 na transiënten even "ademt".
4. Een niveau-afhankelijke LF zoals bij Ch 2, maar dan bewust en per model.

---

## 8. Kalibratiedoelen als tests

`results/summary.csv` bevat per plugin, console en drive (72 rijen):

- `gain_1k_m18`, `gain_1k_0`, `gain_1k_p3`
- `thd_{100,1000,10000}_{-18,-6,0}`
- `H2_1k_m6` … `H10_1k_m6`
- `imd_-18`, `imd_-6`, `imd_0`
- `fr20` … `fr20000` (t.o.v. 1 kHz, −18 dBFS)
- `xtalk_1k`, `latency`

Voorstel voor de repo: een test die Nazaré door dezelfde `nzm.py`-meting haalt en per model controleert of het binnen de toleranties blijft (bijvoorbeeld THD ±3 dB, FR ±0,25 dB, gain ±0,2 dB) ten opzichte van de gekozen referentierij. Waar Nazaré bewust afwijkt (oversampling, spreiding), schrijf je de afwijking in de test uit.

---

## 9. Bestanden

| Bestand | Inhoud |
|---|---|
| `nzm.py` | testsignalen en analyses (harmonischen, FR, IMD, overspraak, ruis, dynamiek, latency) |
| `run_vcc.py` | laadt VCC via pedalboard, meet alle consoles en drive-standen |
| `extra.py` | aliasing, ruis onder signaal, overspraak tegen frequentie |
| `summary.py` | maakt `results/summary.csv` |
| `results/VM_VCC_{Channel,MixBuss}_c{0-5}_d{drive}.json` | volledige meetdata per configuratie (72 bestanden) |
| `results/extra.json` | aliasing, ruis, overspraak per console (drive 18 / 6) |
| `results/summary.csv` | kalibratietabel |

---

## 10. Open punten

1. **Consolenamen invullen** in §3 (volgorde van het VCC-menu).
2. **Waves NLS:** open Waves Central, controleer de licentie en update NLS naar een WaveShell die op macOS 15 laadt. Daarna kan dezelfde meting op NLS Channel en NLS Buss draaien (Spike, Mike, Nevo plus de 32 kanaalvarianten; die varianten zijn juist interessant voor de componentspreiding).
3. Een realtime-controle: bounce in de DAW (route 2 uit de README) om te zien of VCC in realtime anders (overgesampled) rekent.
4. Ruisspectrum en brom onder signaal per console. Nu is alleen het totale niveau gemeten.
