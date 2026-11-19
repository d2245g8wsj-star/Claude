# Nazaré DSP: model en kalibratie

Deze map is de specificatie van het geluid van Nazaré. `model.py` is het
referentiemodel in Python. De JUCE-implementatie moet hetzelfde doen, en
`validate.py` controleert het tegen de meetdoelen.

De doelen komen uit een black-box meting van Slate VCC Channel en MixBuss:
`../measure/reference/vcc/RAPPORT.md` en `summary.csv`, gemeten met
`../measure/` op 48 kHz. Er is niets gedecompileerd of gekopieerd. Elk model
van Nazaré is op karakter aan een referentieconsole gekoppeld en daarop
gefit. GLASS is een eigen ontwerp.

## Wat de metingen ons leerden, en wat Nazaré daarmee doet

| Bevinding bij de referentie | Keuze voor Nazaré |
| --- | --- |
| Het karakter komt volledig uit statische waveshapers met lineaire filters eromheen. | Dezelfde opbouw, gefit per console: `shaper` en `pre` in `models.json`. |
| Drive schaalt de vervormingsterm (ongeveer 1 dB THD per dB drive) en verplaatst het clippunt niet. Alleen de buis is echte ingangsgain. | `drive_mode: coef` voor vijf modellen, `pregain` met gedeeltelijke make-up voor SUPERTUBES. Gain-bump per model (`bump`). |
| Er bestaat een vaste even kleur `y = x + k·|x|`: H2 hangt niet van het niveau af, en H4 ligt precies 14 dB lager. | De `k1`-term. PIPELINE heeft hem (k1 ≈ 0,001), net als de referentie. |
| De vervorming hangt af van de frequentie: transformator (LF) of slew/pre-emphasis (HF). | Pre-shelves vóór de shaper, met het exacte inverse filter erna. De frequentierespons blijft zo onveranderd. Begrensd op ±18 dB, zodat het op muziek realistisch blijft. |
| De frequentierespons is klein (< 1 dB, behalve de LF-boost van +5 dB bij de klasse-A-console) en verandert niet met het niveau. | Per model een hoogdoorlaat, een low shelf, een peak en een high shelf, gefit tot binnen 0,1 dB. |
| Geen oversampling: aliases tot −27 dB. | **4× oversampling** met een FIR van 383 taps: vlak tot 20 kHz, −110 dB bij 24 kHz. Doel: aliasing onder −90 dB. |
| Links en rechts zijn bit-identiek. | **Componentspreiding** per kanaalnummer (1–48), op SPREAD: ±0,15 dB gain, ±2 dB in de even termen, ±4 % clippunt. |
| Volledig geheugenloos. | **Transiëntgeheugen**: na een transiënt zwellen de even termen en de buis-bias ongeveer 100 ms op en zakken dan terug. Bij stilstaande signalen is het effect nul, dus alle statische doelen blijven gelden. |
| Ruis alleen zolang er signaal is; digitale stilte blijft stil. | Ruis met een gate op de ingangsenvelope (release 50 ms). |
| Overspraak alleen op de bus, lineair, per console een eigen vorm (vlak, LF, HF of U-vorm). | In BUS-mode een 2×2-matrix met een vlakke, een LF- en een HF-term per model. |

## De keten, per kanaal

```
in → pre-shelves → [4× omhoog] shaper [4× omlaag] → inverse shelves → toon-EQ → DC-blokker
   → uitgangsgain (+ spreiding) → overspraak (BUS) → ruis met gate → uit
```

De shaper, met `D = 10^((drive − 6)/20)`:

```
x = u · G                              G = 1 (coef) of D · pre_gain (pregain)
v = x + m·k1·D·|x| + m·a2·D·x² + a3·D·x³ + xover·ε·tanh(x/ε)
v = (tanh(g·(v + m·b)) − tanh(g·m·b)) / (g·(1 − tanh²(g·b)))     alleen de buis
v = v / (1 + (|v|/c)^p)^(1/p)                                      zachte clip
v = v / G^makeup                                                   alleen de buis
```

Hier is `m` het transiëntgeheugen: `1 + memory · tanh(max(env_4ms − env_150ms, 0) / 0,25)`.

## Modellen

| Nazaré | Karakter | Referentie | Drive |
| --- | --- | --- | --- |
| GLASS | modern transformatorloos, schoonste | eigen ontwerp | coef |
| PIPELINE | Brits VCA, vaste H2-glans plus groei | Channel console 0 | coef |
| MAVERICKS | Amerikaans discreet, schoon tot de muur | Channel console 2 | coef |
| JAWS | Brits klasse A, transformator-laag | Channel console 3 | coef |
| TEAHUPO'O | germanium, HF-glans, ruw bij zacht | Channel console 4 | coef |
| SUPERTUBES | buis, dik en comprimerend | Channel console 5 | pregain |

De kalibratie, met per model doel tegen resultaat, staat in `CALIBRATION.md`.

## Draaien

```bash
pip install numpy scipy
python fit.py            # fit alle modellen opnieuw (ongeveer 8 minuten); python fit.py jaws fit er één
python validate.py       # meet alle modellen en schrijft CALIBRATION.md
python diag.py jaws      # doel tegen model, per meetpunt, uit de snelle evaluatie
```

## Open punten

- De namen van de referentieconsoles (alleen nummers 0–5 bekend). De koppeling is op karakter gekozen.
- Waves NLS meten zodra WaveShell op macOS 15 laadt. De 32 kanaalvarianten daarvan zijn een goede maat voor de spreiding.
- Een realtime-controle van de referentie via een bounce in de DAW: rekent die realtime anders (overgesampled)?
- Stand van de kalibratie (`CALIBRATION.md`): 91 van de 100 controles binnen tolerantie. De 9 afwijkingen:
  - **MAVERICKS**: 5–7 dB te vuil bij −18 en −6 dBFS op 1 kHz, en 10 dB te schoon bij 100 Hz. De BUS-overspraak bij 1 kHz is −108 in plaats van −120 dB. De "muur" van de referentie gedraagt zich als een slew-limiet; dat volgt een statische shaper maar half. Volgende stap: een echte slew-limiter in de keten.
  - **TEAHUPO'O**: 6 dB te schoon bij −18 dBFS op 1 kHz, en 10 dB te vuil bij 100 Hz. De crossover-component zou HF-afhankelijk moeten zijn.
  - **SUPERTUBES**: 4–7 dB te schoon bij −18 en −6 dBFS, en 0,46 dB te veel gain bij −18 dBFS. Daarnaast is de referentie bij 10 kHz veel schoner in H2 dan bij 1 kHz. Volgende stap: een even term op een laagdoorlaat-gefilterd signaal.
- Onder drive 0 (onder het gekalibreerde bereik) worden bij de buis ook de kleurtermen, de bias en de ingangsgain kleiner, met volledige make-up. Zo zijn de stille zones van de drive-knop (PRISTINE tot SILK) bereikbaar zonder niveausprong.
