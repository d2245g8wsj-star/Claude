# iPod Sync

Een Mac-app om muziek op een **iPod nano (3e generatie)** te zetten en te beheren, zonder iTunes of Finder-synchronisatie.

- **Bekijk** alle muziek en playlists die al op de iPod staan, en zoek erin.
- **Wijzig** titel, artiest, album, genre, jaar, tracknummer en meer, ook voor meerdere nummers tegelijk.
- **Verwijder** nummers, en **maak, hernoem en vul** playlists.
- **Voeg toe:**
  - je eigen bestanden (mp3, m4a, wav, aiff; flac, ogg en opus worden eerst omgezet naar AAC)
  - YouTube-video's en YouTube-playlists
  - Spotify-playlists, -albums en -nummers (zie hieronder hoe dat werkt)
  - losse regels als `Brooke Fraser - Albertine`

> **Over Spotify en YouTube:** muziek van Spotify is beveiligd met DRM en wordt **niet** gedownload. De app leest alleen de lijst met nummers (titel, artiest, album, duur, hoes) en zoekt per nummer dezelfde versie op YouTube. Downloaden van YouTube kan in strijd zijn met de voorwaarden van YouTube. Gebruik het alleen voor muziek waarop je rechten hebt.

Het databaseformaat is gelijk aan dat van andere iPods van die tijd (classic, nano 1G tot 4G, video), dus die zouden ook moeten werken. **Eerlijk gezegd:** de app is getest tegen databases die [libgpod](https://github.com/gtkpod/libgpod) voor een nano 3G maakt, maar nog niet op een echte iPod. Volg daarom de stappen onder *Eerste keer: rustig testen*.

---

## 1. Installeren (eenmalig)

1. Installeer [Homebrew](https://brew.sh) als je dat nog niet hebt.
2. Open **Terminal** en typ:
   ```
   brew install python python-tk ffmpeg
   ```
3. Download deze map (de knop **Code → Download ZIP** op GitHub) en pak hem uit.
4. Dubbelklik op **`start.command`**.
   - Weigert macOS het bestand ("onbekende ontwikkelaar")? Klik dan met de rechtermuisknop op `start.command` → **Open** → **Open**.
   - De eerste keer duurt het een minuut, omdat de app zijn onderdelen installeert.

## 2. De iPod voorbereiden (eenmalig)

1. Sluit de iPod aan en open **Finder**. Klik links op de iPod.
2. Zet op het tabblad **Algemeen** het vinkje aan bij **"Muziek, films en tv-programma's handmatig beheren"** (heet soms "Schijfgebruik inschakelen"). Klik op **Pas toe**.
3. De iPod verschijnt nu als schijf op het bureaublad.
4. **Synchroniseer de iPod daarna niet meer met Finder of iTunes.** Anders overschrijft Finder de wijzigingen die je met deze app maakt.

## 3. Gebruiken

1. Start de app en sluit de iPod aan. De app vindt hem zelf. Lukt dat niet, klik dan op **Zoeken** of op **Map kiezen…** en kies de iPod onder `/Volumes`.
2. Tabblad **Op de iPod**:
   - Links kies je **Alle nummers** of een playlist. Rechts zie je de nummers. Klik op een kolomkop om te sorteren.
   - **Dubbelklik** op een nummer, of selecteer er een paar en klik op **Bewerken…**.
   - **Verwijderen van iPod**, **Aan playlist toevoegen…**, **Uit playlist halen** en **▲▼** (volgorde in een playlist).
   - Wijzigingen staan pas op de iPod als je op **Opslaan naar iPod** klikt.
3. Tabblad **Toevoegen**:
   - Plak links (één per regel) en klik op **Ophalen**, of klik op **Eigen bestanden kiezen…** of **Map kiezen…**.
   - Controleer in de kolom **Gevonden audio** welke YouTube-video de app heeft gekozen. Klopt die niet, dubbelklik dan op de regel en kies een andere versie, of plak zelf een link.
   - Kies eventueel bij **Ook in playlist** een bestaande playlist of typ een nieuwe naam.
   - Klik op **Op iPod zetten**. De app downloadt, zet de tags goed, kopieert de bestanden en slaat meteen op. Nummers die er al op staan, worden overgeslagen.
4. Klik op **Uitwerpen** voordat je de kabel loskoppelt.

### Spotify-sleutels (optioneel, maar aanbevolen)

Zonder sleutels leest de app openbare Spotify-playlists via de openbare "embed"-pagina. Dat werkt tot ongeveer 100 nummers en zonder albumnaam. Met eigen, gratis sleutels werkt het beter:

1. Ga naar <https://developer.spotify.com/dashboard> en log in met je Spotify-account.
2. Klik op **Create app**. Vul een naam in, gebruik als Redirect URI bijvoorbeeld `http://127.0.0.1:8080`, en vink **Web API** aan.
3. Kopieer de **Client ID** en de **Client Secret** naar **⚙︎ Instellingen** in de app.

Privé-playlists en door Spotify gemaakte lijsten (zoals "Mix van de dag") zijn via de API niet op te vragen. Maak er eventueel een kopie van als eigen, openbare playlist.

---

## Veiligheid en back-ups

Een iPod nano 3G accepteert zijn database alleen met een digitale handtekening (**hash58**), die hoort bij de **FireWire-ID** van jouw iPod. De app:

- zoekt die ID zelf op (in `iPod_Control/Device/SysInfo`, of via Systeeminformatie);
- **controleert** vóór elke schrijfactie of de ID klopt, door de handtekening van de bestaande database na te rekenen. Klopt die niet, dan schrijft de app **niets**;
- maakt vóór elke opslag een **back-up** van de database in `~/Library/Application Support/iPodSync/backups/`. De allereerste versie heet `iTunesDB-origineel` en wordt nooit weggegooid. Van de latere versies bewaart de app er 10;
- schrijft eerst naar een tijdelijk bestand, leest dat opnieuw in ter controle, en vervangt dan pas het origineel.

Terugzetten gaat via **Back-up terugzetten…** in het tabblad **Op de iPod**.

### FireWire-ID vinden (alleen als de app daarom vraagt)

Apple-menu → **Over deze Mac** → **Meer info…** → **Systeemrapport** → **USB** → **iPod** → **Serienummer**. Dat zijn 16 tekens, zoals `000A27001234ABCD`. Vul ze in bij **⚙︎ Instellingen → FireWire-ID**.

### Eerste keer: rustig testen

1. Open de iPod in de app en kijk alleen (de app schrijft pas iets als je op Opslaan klikt).
2. Zet één nummer erop, werp de iPod uit en controleer of het nummer speelt.
3. Probeer daarna bewerken, verwijderen en playlists.

Toont de iPod ooit "Geen muziek"? Zet dan de laatste back-up terug. Helpt dat niet, dan kun je de iPod in het uiterste geval herstellen via Finder.

## Beperkingen

- **Albumhoezen** worden in de bestanden zelf gezet (je ziet ze op de computer), maar nog **niet op het iPod-scherm**. Daarvoor is ook de ArtworkDB van de iPod nodig; dat staat op de planning. Bestaande hoezen op de iPod blijven gewoon staan.
- De **On-The-Go-playlist** (de playlist die je op de iPod zelf maakt) wordt bij opslaan leeggemaakt.
- **Afspeeltellingen en beoordelingen** die de iPod heeft bijgehouden, worden bij opslaan netjes verwerkt.
- YouTube verandert regelmatig. `start.command` werkt daarom bij elke start `yt-dlp` bij. Mislukt downloaden toch, start de app dan opnieuw.

---

## Voor ontwikkelaars

```
python3 -m venv .venv && .venv/bin/pip install -e ".[dev]"
.venv/bin/pytest
```

- `src/ipodsync/ipod/itunesdb.py`: lezen en schrijven van de iTunesDB. Onbekende velden en secties blijven byte voor byte behouden. Tracks, playlists en bibliotheekindexen zijn in de tests byte-identiek aan wat libgpod schrijft.
- `src/ipodsync/ipod/hash58.py`: port van `itdb_hash58.c`, getest tegen de C-implementatie.
- `src/ipodsync/ipod/library.py`: toevoegen, wijzigen, verwijderen, playlists, back-ups en veilig opslaan.
- `src/ipodsync/sources/`: Spotify, YouTube, matchen en de verwerkingsstappen.
- `src/ipodsync/gui/`: de Tkinter-interface.
- `tools/gpodtool.c`: klein hulpprogramma op basis van libgpod om een test-iPod te maken en een database onafhankelijk terug te lezen:
  `gcc -o gpodtool tools/gpodtool.c $(pkg-config --cflags --libs libgpod-1.0)`.

### Licentie en dank

Het databaseformaat en de hash58-code zijn gebaseerd op [libgpod](https://github.com/gtkpod/libgpod) (LGPL-2.1; hash58 onder een BSD-licentie). Deze app valt daarom ook onder de **LGPL-2.1-or-later**. Het testbestand `tests/fixtures/empty-v0d.iTunesDB` komt uit de testset van libgpod.
