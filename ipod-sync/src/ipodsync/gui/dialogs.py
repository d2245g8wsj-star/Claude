"""Dialoogvensters: tags bewerken, instellingen, match kiezen, back-ups."""

from __future__ import annotations

import tkinter as tk
from tkinter import messagebox, ttk
from pathlib import Path

from ..config import Config
from ..ipod.itunesdb import Track
from ..sources.matcher import Candidate

TAG_LABELS = [
    ("title", "Titel"),
    ("artist", "Artiest"),
    ("album", "Album"),
    ("albumartist", "Albumartiest"),
    ("genre", "Genre"),
    ("composer", "Componist"),
    ("year", "Jaar"),
    ("track_nr", "Tracknummer"),
    ("tracks_total", "Aantal tracks"),
    ("cd_nr", "Cd-nummer"),
]


class _Dialog(tk.Toplevel):
    def __init__(self, parent, title: str):
        super().__init__(parent)
        self.title(title)
        self.transient(parent)
        self.resizable(False, False)
        self.result = None
        self.body = ttk.Frame(self, padding=14)
        self.body.pack(fill="both", expand=True)
        self.bind("<Escape>", lambda e: self.destroy())

    def buttons(self, ok_text: str = "OK"):
        bar = ttk.Frame(self.body)
        bar.grid(row=99, column=0, columnspan=3, sticky="e", pady=(12, 0))
        ttk.Button(bar, text="Annuleren", command=self.destroy).pack(side="right")
        ttk.Button(bar, text=ok_text, command=self.on_ok, style="Accent.TButton").pack(side="right", padx=6)
        self.bind("<Return>", lambda e: self.on_ok())

    def on_ok(self):
        self.destroy()

    def run(self):
        self.grab_set()
        self.wait_window()
        return self.result


class TagEditor(_Dialog):
    """Tags van één of meer nummers bewerken. Bij meerdere nummers: leeg = ongewijzigd."""

    def __init__(self, parent, tracks: list[Track]):
        multi = len(tracks) > 1
        super().__init__(parent, f"{len(tracks)} nummers bewerken" if multi else "Nummer bewerken")
        self.vars: dict[str, tk.StringVar] = {}
        self.initial: dict[str, str] = {}
        fields = TAG_LABELS if not multi else [f for f in TAG_LABELS if f[0] not in ("title", "track_nr")]
        for row, (key, label) in enumerate(fields):
            values = {self._value(t, key) for t in tracks}
            val = values.pop() if len(values) == 1 else ""
            ttk.Label(self.body, text=label).grid(row=row, column=0, sticky="w", pady=2)
            var = tk.StringVar(value=val)
            e = ttk.Entry(self.body, textvariable=var, width=42)
            e.grid(row=row, column=1, sticky="we", pady=2)
            if multi and not val and len({self._value(t, key) for t in tracks}) > 1:
                ttk.Label(self.body, text="(verschilt)", foreground="gray").grid(row=row, column=2, padx=4)
            self.vars[key] = var
            self.initial[key] = val
            if row == 0:
                e.focus_set()
        if multi:
            ttk.Label(
                self.body, text="Lege velden die verschillen, blijven per nummer ongewijzigd.",
                foreground="gray",
            ).grid(row=50, column=0, columnspan=3, sticky="w", pady=(8, 0))
        self.buttons("Bewaren")

    @staticmethod
    def _value(t: Track, key: str) -> str:
        v = getattr(t, key)
        if isinstance(v, int):
            return str(v) if v else ""
        return v or ""

    def on_ok(self):
        changed = {k: v.get() for k, v in self.vars.items() if v.get() != self.initial[k]}
        for k in ("year", "track_nr", "tracks_total", "cd_nr"):
            if k in changed and changed[k].strip() and not changed[k].strip().isdigit():
                messagebox.showerror("Ongeldige waarde", f"'{changed[k]}' is geen getal.", parent=self)
                return
        self.result = changed
        self.destroy()


class SettingsDialog(_Dialog):
    def __init__(self, parent, cfg: Config, ipod_name: str | None, detected_guid: str | None):
        super().__init__(parent, "Instellingen")
        self.cfg = cfg
        self.ipod_name = ipod_name
        r = 0
        ttk.Label(self.body, text="Spotify (optioneel)", font=("TkDefaultFont", 12, "bold")).grid(
            row=r, column=0, columnspan=2, sticky="w")
        r += 1
        ttk.Label(
            self.body, wraplength=440, foreground="gray",
            text="Zonder sleutels leest de app openbare playlists tot ongeveer 100 nummers. Met een gratis "
                 "Client ID/Secret van developer.spotify.com (Dashboard → Create app) werkt het "
                 "betrouwbaarder en krijg je ook albumgegevens.",
        ).grid(row=r, column=0, columnspan=2, sticky="w", pady=(0, 6))
        r += 1
        self.cid = tk.StringVar(value=cfg.spotify_client_id)
        self.secret = tk.StringVar(value=cfg.spotify_client_secret)
        ttk.Label(self.body, text="Client ID").grid(row=r, column=0, sticky="w")
        ttk.Entry(self.body, textvariable=self.cid, width=44).grid(row=r, column=1, pady=2)
        r += 1
        ttk.Label(self.body, text="Client Secret").grid(row=r, column=0, sticky="w")
        ttk.Entry(self.body, textvariable=self.secret, width=44, show="•").grid(row=r, column=1, pady=2)
        r += 1

        ttk.Separator(self.body).grid(row=r, column=0, columnspan=2, sticky="we", pady=10)
        r += 1
        ttk.Label(self.body, text="Audio", font=("TkDefaultFont", 12, "bold")).grid(row=r, column=0, sticky="w")
        r += 1
        ttk.Label(self.body, text="AAC-kwaliteit bij omzetten").grid(row=r, column=0, sticky="w")
        self.bitrate = tk.StringVar(value=cfg.aac_bitrate)
        ttk.Combobox(self.body, textvariable=self.bitrate, values=["128k", "192k", "256k", "320k"],
                     width=8, state="readonly").grid(row=r, column=1, sticky="w")
        r += 1
        self.keep = tk.BooleanVar(value=cfg.keep_downloads)
        ttk.Checkbutton(self.body, text="Downloads bewaren na het toevoegen", variable=self.keep).grid(
            row=r, column=1, sticky="w", pady=2)
        r += 1

        ttk.Separator(self.body).grid(row=r, column=0, columnspan=2, sticky="we", pady=10)
        r += 1
        ttk.Label(self.body, text="iPod", font=("TkDefaultFont", 12, "bold")).grid(row=r, column=0, sticky="w")
        r += 1
        self.guid = tk.StringVar(value=cfg.firewire_guids.get(ipod_name or "", ""))
        ttk.Label(self.body, text="FireWire-ID").grid(row=r, column=0, sticky="w")
        state = "normal" if ipod_name else "disabled"
        ttk.Entry(self.body, textvariable=self.guid, width=24, state=state).grid(row=r, column=1, sticky="w")
        r += 1
        hint = f"Automatisch gevonden: {detected_guid}" if detected_guid else (
            "Niet automatisch gevonden. Zie de README ('FireWire-ID vinden'). "
            "Het is het serienummer van de iPod in Systeeminformatie → USB (16 tekens).")
        ttk.Label(self.body, text=hint + "\nLaat leeg om de automatisch gevonden waarde te gebruiken.",
                  wraplength=440, foreground="gray").grid(row=r, column=0, columnspan=2, sticky="w")
        self.buttons("Bewaren")

    def on_ok(self):
        from ..ipod.hash58 import parse_firewire_id

        guid = self.guid.get().strip()
        if guid:
            try:
                if len(parse_firewire_id(guid).rstrip(b"\0")) > 8 or len(guid.removeprefix("0x")) != 16:
                    raise ValueError
            except ValueError:
                messagebox.showerror("Ongeldige FireWire-ID",
                                     "Verwacht 16 hexadecimale tekens, bijv. 000A27001234ABCD.", parent=self)
                return
        self.cfg.spotify_client_id = self.cid.get().strip()
        self.cfg.spotify_client_secret = self.secret.get().strip()
        self.cfg.aac_bitrate = self.bitrate.get()
        self.cfg.keep_downloads = self.keep.get()
        if self.ipod_name:
            if guid:
                self.cfg.firewire_guids[self.ipod_name] = guid
            else:
                self.cfg.firewire_guids.pop(self.ipod_name, None)
        self.cfg.save()
        self.result = True
        self.destroy()


class MatchChooser(_Dialog):
    """Een andere YouTube-video kiezen, of zelf een link plakken."""

    def __init__(self, parent, heading: str, candidates: list[Candidate]):
        super().__init__(parent, "Andere versie kiezen")
        self.resizable(True, True)
        self.candidates = candidates
        ttk.Label(self.body, text=heading, font=("TkDefaultFont", 12, "bold")).grid(
            row=0, column=0, columnspan=2, sticky="w")
        cols = ("score", "duur", "kanaal")
        self.tree = ttk.Treeview(self.body, columns=cols, height=8, selectmode="browse")
        self.tree.heading("#0", text="Video")
        self.tree.heading("score", text="Score")
        self.tree.heading("duur", text="Duur")
        self.tree.heading("kanaal", text="Kanaal")
        self.tree.column("#0", width=360)
        self.tree.column("score", width=60, anchor="e")
        self.tree.column("duur", width=60, anchor="e")
        self.tree.column("kanaal", width=180)
        for i, c in enumerate(candidates):
            d = int(c.duration_s)
            self.tree.insert("", "end", iid=str(i), text=c.title,
                             values=(f"{c.score:.0f}", f"{d // 60}:{d % 60:02d}", c.channel))
        self.tree.grid(row=1, column=0, columnspan=2, sticky="nsew", pady=8)
        self.tree.bind("<Double-1>", lambda e: self.on_ok())
        ttk.Label(self.body, text="Of plak een YouTube-link:").grid(row=2, column=0, sticky="w")
        self.url = tk.StringVar()
        ttk.Entry(self.body, textvariable=self.url, width=50).grid(row=2, column=1, sticky="we")
        self.body.columnconfigure(1, weight=1)
        self.body.rowconfigure(1, weight=1)
        self.buttons("Kiezen")

    def on_ok(self):
        url = self.url.get().strip()
        if url:
            self.result = Candidate(url=url, title=url)
        else:
            sel = self.tree.selection()
            if not sel:
                return
            self.result = self.candidates[int(sel[0])]
        self.destroy()


class BackupDialog(_Dialog):
    def __init__(self, parent, backups: list[Path]):
        super().__init__(parent, "Back-up terugzetten")
        ttk.Label(
            self.body, wraplength=420,
            text="Kies een eerdere versie van de iPod-database. Let op: nummers die je sindsdien hebt "
                 "verwijderd, komen niet terug (de bestanden zijn weg). 'origineel' is de versie van "
                 "vóór het eerste gebruik van deze app.",
        ).grid(row=0, column=0, sticky="w")
        self.lb = tk.Listbox(self.body, height=10, width=50)
        for b in backups:
            self.lb.insert("end", b.name.replace("iTunesDB-", ""))
        self.lb.grid(row=1, column=0, sticky="nsew", pady=8)
        self.backups = backups
        self.buttons("Terugzetten")

    def on_ok(self):
        sel = self.lb.curselection()
        if not sel:
            return
        if messagebox.askyesno("Zeker weten?", "De huidige database op de iPod wordt vervangen.", parent=self):
            self.result = self.backups[sel[0]]
            self.destroy()


def ask_string(parent, title: str, prompt: str, initial: str = "") -> str | None:
    d = _Dialog(parent, title)
    ttk.Label(d.body, text=prompt).grid(row=0, column=0, sticky="w")
    var = tk.StringVar(value=initial)
    e = ttk.Entry(d.body, textvariable=var, width=40)
    e.grid(row=1, column=0, pady=6)
    e.focus_set()
    e.select_range(0, "end")

    def ok():
        d.result = var.get().strip() or None
        d.destroy()

    d.on_ok = ok
    d.buttons()
    return d.run()


def choose_playlist(parent, names: list[str]) -> str | None:
    """Kies een bestaande playlist of typ een nieuwe naam."""
    d = _Dialog(parent, "Aan playlist toevoegen")
    ttk.Label(d.body, text="Playlist (kies of typ een nieuwe naam):").grid(row=0, column=0, sticky="w")
    var = tk.StringVar(value=names[0] if names else "")
    cb = ttk.Combobox(d.body, textvariable=var, values=names, width=38)
    cb.grid(row=1, column=0, pady=6)
    cb.focus_set()

    def ok():
        d.result = var.get().strip() or None
        d.destroy()

    d.on_ok = ok
    d.buttons("Toevoegen")
    return d.run()
