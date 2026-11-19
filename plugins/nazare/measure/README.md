# Nazaré: referentiemetingen van Slate VCC en Waves NLS

Deze map meet console-plugins van buitenaf: testsignaal erin, audio eruit, en
daaruit de cijfers waarmee we de zes modellen van Nazaré kalibreren. Er wordt
niets gedecompileerd of gekopieerd; alleen de audio die de plugin teruggeeft
wordt gelezen.

Wat er per console en per drive-stand gemeten wordt:

- vervorming (THD) en de harmonischen H2 tot H10, tegen niveau, op 100 Hz, 1 kHz en 10 kHz
- versterking tegen niveau (waar de compressie begint)
- frequentierespons op −30, −18 en −6 dBFS
- twee-toon intermodulatie (SMPTE, 60 Hz + 7 kHz)
- overspraak van links naar rechts en andersom
- ruisvloer en brom
- verschil tussen links en rechts (componentspreiding)
- hoe gain en harmonischen bewegen na een niveausprong (geheugen, dynamische bias)
- latency

## Route 1: automatisch, op je eigen computer (aanbevolen)

Nodig: Python 3.10 of nieuwer, en VCC en NLS geactiveerd op die computer
(iLok of Waves Central). Zonder licentie laden ze niet.

```bash
cd plugins/nazare/measure
python3 -m venv .venv && source .venv/bin/activate      # Windows: .venv\Scripts\activate
pip install pedalboard numpy
python run_plugins.py --scan      # laat zien welke Slate- en Waves-plugins gevonden zijn
python run_plugins.py --all       # meet ze allemaal; duurt een paar minuten per plugin
```

De resultaten komen in `results/`: per plugin een `params.json` (alle
parameters met hun bereik), een JSON per console en drive-stand, en een
`summary.csv`. Commit en push alleen de map `results/`.

Kiest het script de verkeerde parameter voor console of drive, kijk dan in
`params.json` hoe ze heten en meet één plugin gericht:

```bash
python run_plugins.py --params "/Library/Audio/Plug-Ins/VST3/WaveShell1-VST3 15.0.vst3" --name "NLS Channel Stereo"
python run_plugins.py --plugin "<pad naar .vst3 of .component>" --name "<naam in de shell>" --drives 0,0.2,0.4,0.6,0.8,1
```

### Met Claude Code op je computer

Open in de Claude Desktop-app een Claude Code-sessie in je clone van deze
repo, op branch `claude/funny-hamilton-l21wnn`, en geef deze opdracht:

> Volg plugins/nazare/measure/README.md, route 1. Maak de venv, installeer
> pedalboard en numpy, draai `run_plugins.py --scan` en daarna `--all`. Kies
> de stereo-versies van de VCC-plugins (Channel en Mixbuss) en van Waves NLS
> (Channel en Buss). Controleer per plugin in params.json of de console- en
> drive-parameter goed gekozen zijn; zo niet, meet die plugin opnieuw met
> --plugin, --name en de juiste parameter. Commit daarna alleen
> plugins/nazare/measure/results/ en push naar dezelfde branch.

## Route 2: met de hand in je DAW

```bash
python daw_route.py make          # maakt nazare_testsignal.wav (95 s, 48 kHz, 32-bit float)
```

1. Zet `nazare_testsignal.wav` op een stereospoor in een project op 48 kHz.
2. Zet de console-plugin erop, kies console en drive, en zet ruis uit als dat kan.
3. Bounce het hele bestand, zonder iets anders op het spoor of de master.
4. Noem de export naar de instelling, bijvoorbeeld `vcc-channel_brit-n_drive50.wav`.
5. Doe dat per console voor drive 0, 25, 50, 75 en 100 %.
6. Zet de exports in `results/exports/` en push ze, of draai zelf
   `python daw_route.py analyse results/exports/*.wav`.

## Bestanden

| Bestand | Wat |
| --- | --- |
| `nzmeasure.py` | het testsignaal en alle analyses |
| `run_plugins.py` | zoekt de plugins, laadt ze met pedalboard en meet ze |
| `daw_route.py` | maakt het testbestand en analyseert exports uit een DAW |
| `results/` | de metingen; dit is wat terug moet komen |
