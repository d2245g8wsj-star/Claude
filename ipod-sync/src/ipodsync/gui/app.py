"""Hoofdvenster van iPod Sync."""

from __future__ import annotations

import queue
import shutil
import tempfile
import threading
import tkinter as tk
import traceback
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from ..audio import SUPPORTED_EXTENSIONS
from ..config import Config, download_dir
from ..ipod.device import IPodDevice, find_ipods
from ..ipod.itunesdb import Playlist, Track
from ..ipod.library import IPodLibrary, LibraryError
from ..sources import pipeline
from ..sources.parse import parse_input
from . import dialogs

ALL_SONGS = "__all__"
LEGAL_NOTE = (
    "Spotify-nummers worden niet van Spotify gedownload: de app zoekt dezelfde versie op YouTube. "
    "Gebruik dit alleen voor muziek waarop je rechten hebt."
)


def fmt_duration(ms: int) -> str:
    s = ms // 1000
    return f"{s // 60}:{s % 60:02d}"


def fmt_size(n: int) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.1f} {unit}" if unit != "B" else f"{n} B"
        n /= 1024
    return str(n)


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("iPod Sync")
        self.geometry("1100x700")
        self.minsize(900, 560)
        self.cfg = Config.load()
        self.devices: list[IPodDevice] = []
        self.lib: IPodLibrary | None = None
        self.queue_items: list[pipeline.QueueItem] = []
        self.busy = False
        self._events: queue.Queue = queue.Queue()
        self._workdir = Path(tempfile.mkdtemp(prefix="ipodsync-"))

        style = ttk.Style(self)
        style.configure("Accent.TButton", font=("TkDefaultFont", 12, "bold"))
        style.configure("Treeview", rowheight=22)

        self._build_topbar()
        self._build_tabs()
        self._build_statusbar()
        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self.after(100, self._poll_events)
        self.after(200, self.refresh_devices)

    # ================================================================== layout

    def _build_topbar(self):
        bar = ttk.Frame(self, padding=(10, 8))
        bar.pack(fill="x")
        ttk.Label(bar, text="iPod:").pack(side="left")
        self.device_var = tk.StringVar()
        self.device_cb = ttk.Combobox(bar, textvariable=self.device_var, state="readonly", width=28)
        self.device_cb.pack(side="left", padx=4)
        self.device_cb.bind("<<ComboboxSelected>>", lambda e: self.open_selected_device())
        ttk.Button(bar, text="Zoeken", command=self.refresh_devices).pack(side="left")
        ttk.Button(bar, text="Map kiezen…", command=self.choose_mountpoint).pack(side="left", padx=4)

        ttk.Button(bar, text="⚙︎ Instellingen", command=self.open_settings).pack(side="right")
        ttk.Button(bar, text="Uitwerpen", command=self.eject).pack(side="right", padx=4)
        self.save_btn = ttk.Button(bar, text="Opslaan naar iPod", style="Accent.TButton",
                                   command=self.save, state="disabled")
        self.save_btn.pack(side="right", padx=4)

        info = ttk.Frame(self, padding=(10, 0))
        info.pack(fill="x")
        self.info_var = tk.StringVar(value="Geen iPod geopend")
        ttk.Label(info, textvariable=self.info_var, foreground="gray").pack(side="left")
        self.space = ttk.Progressbar(info, mode="determinate", maximum=100, length=220)
        self.space.pack(side="right")

    def _build_tabs(self):
        self.tabs = ttk.Notebook(self)
        self.tabs.pack(fill="both", expand=True, padx=10, pady=8)
        self.tabs.add(self._build_library_tab(), text="  Op de iPod  ")
        self.tabs.add(self._build_add_tab(), text="  Toevoegen  ")

    def _build_library_tab(self):
        frame = ttk.Frame(self.tabs, padding=6)
        panes = ttk.PanedWindow(frame, orient="horizontal")
        panes.pack(fill="both", expand=True)

        left = ttk.Frame(panes)
        ttk.Label(left, text="Bibliotheek", font=("TkDefaultFont", 12, "bold")).pack(anchor="w")
        self.pl_tree = ttk.Treeview(left, show="tree", selectmode="browse", height=12)
        self.pl_tree.pack(fill="both", expand=True, pady=4)
        self.pl_tree.bind("<<TreeviewSelect>>", lambda e: self.show_tracks())
        plbar = ttk.Frame(left)
        plbar.pack(fill="x")
        ttk.Button(plbar, text="+ Playlist", command=self.new_playlist).pack(side="left")
        ttk.Button(plbar, text="Hernoem", command=self.rename_playlist).pack(side="left", padx=2)
        ttk.Button(plbar, text="Verwijder", command=self.delete_playlist).pack(side="left")
        ttk.Separator(left).pack(fill="x", pady=8)
        ttk.Button(left, text="Back-up terugzetten…", command=self.restore_backup).pack(fill="x")
        panes.add(left, weight=1)

        right = ttk.Frame(panes)
        sbar = ttk.Frame(right)
        sbar.pack(fill="x")
        ttk.Label(sbar, text="Zoeken:").pack(side="left")
        self.search_var = tk.StringVar()
        self.search_var.trace_add("write", lambda *a: self.show_tracks())
        ttk.Entry(sbar, textvariable=self.search_var, width=30).pack(side="left", padx=4)
        self.count_var = tk.StringVar()
        ttk.Label(sbar, textvariable=self.count_var, foreground="gray").pack(side="right")

        cols = ("artist", "album", "genre", "time", "plays")
        tf = ttk.Frame(right)
        tf.pack(fill="both", expand=True, pady=4)
        self.tree = ttk.Treeview(tf, columns=cols, selectmode="extended")
        for col, label, width, anchor in (
            ("#0", "Titel", 240, "w"), ("artist", "Artiest", 160, "w"), ("album", "Album", 160, "w"),
            ("genre", "Genre", 90, "w"), ("time", "Duur", 60, "e"), ("plays", "Gespeeld", 70, "e"),
        ):
            self.tree.heading(col, text=label, command=lambda c=col: self.sort_by(c))
            self.tree.column(col, width=width, anchor=anchor)
        vsb = ttk.Scrollbar(tf, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        self.tree.bind("<Double-1>", lambda e: self.edit_tracks())
        self.tree.bind("<Delete>", lambda e: self.delete_tracks())
        self.tree.bind("<BackSpace>", lambda e: self.delete_tracks())

        abar = ttk.Frame(right)
        abar.pack(fill="x")
        ttk.Button(abar, text="Bewerken…", command=self.edit_tracks).pack(side="left")
        ttk.Button(abar, text="Aan playlist toevoegen…", command=self.add_selection_to_playlist).pack(side="left", padx=4)
        ttk.Button(abar, text="Uit playlist halen", command=self.remove_from_playlist).pack(side="left")
        ttk.Button(abar, text="▲", width=3, command=lambda: self.move_in_playlist(-1)).pack(side="left", padx=(8, 0))
        ttk.Button(abar, text="▼", width=3, command=lambda: self.move_in_playlist(1)).pack(side="left")
        ttk.Button(abar, text="Verwijderen van iPod", command=self.delete_tracks).pack(side="right")
        panes.add(right, weight=4)
        self.after(50, lambda: panes.sashpos(0, 300))
        self._sort = ("", False)
        return frame

    def _build_add_tab(self):
        frame = ttk.Frame(self.tabs, padding=6)
        ttk.Label(
            frame, font=("TkDefaultFont", 12, "bold"),
            text="Plak Spotify- of YouTube-links, of regels als 'Artiest - Titel' (één per regel):",
        ).pack(anchor="w")
        self.input = tk.Text(frame, height=5, wrap="none", font=("TkFixedFont", 12))
        self.input.pack(fill="x", pady=4)
        ibar = ttk.Frame(frame)
        ibar.pack(fill="x")
        ttk.Button(ibar, text="Ophalen", style="Accent.TButton", command=self.resolve_input).pack(side="left")
        ttk.Button(ibar, text="Eigen bestanden kiezen…", command=self.choose_files).pack(side="left", padx=6)
        ttk.Button(ibar, text="Map kiezen…", command=self.choose_folder).pack(side="left")
        ttk.Label(frame, text=LEGAL_NOTE, foreground="gray", wraplength=900).pack(anchor="w", pady=(6, 2))

        cols = ("status", "source", "match")
        tf = ttk.Frame(frame)
        tf.pack(fill="both", expand=True, pady=4)
        self.qtree = ttk.Treeview(tf, columns=cols, selectmode="extended")
        for col, label, width in (("#0", "Nummer", 320), ("status", "Status", 110),
                                  ("source", "Bron", 80), ("match", "Gevonden audio", 380)):
            self.qtree.heading(col, text=label)
            self.qtree.column(col, width=width)
        vsb = ttk.Scrollbar(tf, orient="vertical", command=self.qtree.yview)
        self.qtree.configure(yscrollcommand=vsb.set)
        self.qtree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        self.qtree.bind("<Double-1>", lambda e: self.choose_other_match())

        qbar = ttk.Frame(frame)
        qbar.pack(fill="x")
        ttk.Button(qbar, text="Andere versie kiezen…", command=self.choose_other_match).pack(side="left")
        ttk.Button(qbar, text="Uit lijst halen", command=self.remove_queue_items).pack(side="left", padx=4)
        ttk.Button(qbar, text="Lijst leegmaken", command=self.clear_queue).pack(side="left")
        ttk.Label(qbar, text="Ook in playlist:").pack(side="left", padx=(20, 4))
        self.target_pl = tk.StringVar(value="")
        self.target_pl_cb = ttk.Combobox(qbar, textvariable=self.target_pl, width=24)
        self.target_pl_cb.pack(side="left")
        ttk.Button(qbar, text="Op iPod zetten", style="Accent.TButton", command=self.add_queue_to_ipod).pack(side="right")
        return frame

    def _build_statusbar(self):
        bar = ttk.Frame(self, padding=(10, 4))
        bar.pack(fill="x", side="bottom")
        self.status_var = tk.StringVar(value="Klaar")
        ttk.Label(bar, textvariable=self.status_var).pack(side="left")
        self.progress = ttk.Progressbar(bar, mode="determinate", length=260, maximum=1.0)
        self.progress.pack(side="right")

    # ================================================================== achtergrondtaken

    def run_task(self, label: str, fn, on_done=None):
        """Voer fn(report) uit in een thread; report(tekst, fractie) werkt de statusbalk bij."""
        if self.busy:
            messagebox.showinfo("Even geduld", "Er loopt nog een taak.")
            return
        self.busy = True
        self.status(label, 0)

        def report(text=None, frac=None):
            self._events.put(("status", text, frac))

        def worker():
            try:
                result = fn(report)
                self._events.put(("done", on_done, result))
            except Exception as e:  # noqa: BLE001 - alles tonen aan de gebruiker
                traceback.print_exc()
                self._events.put(("error", label, e))

        threading.Thread(target=worker, daemon=True).start()

    def _poll_events(self):
        try:
            while True:
                ev = self._events.get_nowait()
                if ev[0] == "status":
                    self.status(ev[1], ev[2])
                elif ev[0] == "refresh_queue":
                    self.render_queue()
                elif ev[0] == "call":
                    ev[1](ev[2])
                elif ev[0] == "done":
                    self.busy = False
                    self.status("Klaar", 1.0)
                    if ev[1]:
                        ev[1](ev[2])
                elif ev[0] == "error":
                    self.busy = False
                    self.status("Mislukt", 0)
                    if self.lib:
                        self.render_all()
                    self.render_queue()
                    messagebox.showerror(ev[1], str(ev[2]))
        except queue.Empty:
            pass
        self.after(100, self._poll_events)

    def status(self, text=None, frac=None):
        if text is not None:
            self.status_var.set(text)
        if frac is not None:
            self.progress["value"] = frac

    # ================================================================== iPod

    def refresh_devices(self):
        self.devices = find_ipods()
        names = [f"{d.name}  ({d.mountpoint})" for d in self.devices]
        self.device_cb["values"] = names
        if self.devices and not self.lib:
            self.device_cb.current(0)
            self.open_device(self.devices[0])
        elif not self.devices and not self.lib:
            self.info_var.set(
                "Geen iPod gevonden. Sluit hem aan en zet in Finder 'Schijfgebruik inschakelen' aan (zie README)."
            )

    def choose_mountpoint(self):
        d = filedialog.askdirectory(title="Kies het iPod-volume (met de map iPod_Control)", initialdir="/Volumes")
        if not d:
            return
        dev = IPodDevice(Path(d))
        if not dev.control_dir.is_dir():
            messagebox.showerror("Geen iPod", "In deze map staat geen iPod_Control-map.")
            return
        self.devices.append(dev)
        self.device_cb["values"] = list(self.device_cb["values"]) + [f"{dev.name}  ({dev.mountpoint})"]
        self.device_cb.current(len(self.devices) - 1)
        self.open_device(dev)

    def open_selected_device(self):
        i = self.device_cb.current()
        if 0 <= i < len(self.devices):
            self.open_device(self.devices[i])

    def open_device(self, dev: IPodDevice):
        if self.lib and self.lib.dirty and not self._confirm_discard():
            return
        guid = self.cfg.firewire_guids.get(dev.name) or None

        def load(report):
            lib = IPodLibrary(dev, firewire_guid=guid)
            lib.load()
            return lib

        def done(lib: IPodLibrary):
            self.lib = lib
            self.render_all()
            if lib.needs_hash() and not lib.firewire_guid():
                messagebox.showwarning(
                    "FireWire-ID nodig",
                    "Deze iPod vereist een ondertekende database, maar de FireWire-ID is niet "
                    "automatisch gevonden. Je kunt de muziek bekijken, maar opslaan lukt pas als je de "
                    "ID invult bij Instellingen (zie README).",
                )

        self.run_task(f"{dev.name} lezen…", load, done)

    def _confirm_discard(self) -> bool:
        return messagebox.askyesno("Niet opgeslagen", "Er zijn wijzigingen die nog niet op de iPod staan. Weggooien?")

    def mark_dirty(self):
        self.save_btn["state"] = "normal" if self.lib and self.lib.dirty else "disabled"
        if self.lib and self.lib.dirty:
            self.status("Wijzigingen nog niet opgeslagen. Klik op 'Opslaan naar iPod'.")

    def save(self):
        if not self.lib:
            return
        lib = self.lib

        def work(report):
            return lib.save(progress=report)

        def done(backup):
            self.render_all()
            self.status(f"Opgeslagen. Back-up van de vorige versie: {backup.name if backup else '—'}", 1.0)

        self.run_task("Opslaan naar iPod…", work, done)

    def eject(self):
        if not self.lib:
            return
        if self.lib.dirty and not messagebox.askyesno(
            "Niet opgeslagen", "Er zijn niet-opgeslagen wijzigingen. Toch uitwerpen?"
        ):
            return
        try:
            self.lib.device.eject()
        except Exception as e:  # noqa: BLE001
            messagebox.showerror("Uitwerpen mislukt", f"{e}\n\nSluit programma's die de iPod gebruiken.")
            return
        self.lib = None
        self.render_all()
        self.info_var.set("iPod uitgeworpen. Je kunt hem loskoppelen.")

    def restore_backup(self):
        if not self.lib:
            return
        backups = self.lib.list_backups()
        if not backups:
            messagebox.showinfo("Geen back-ups", "Er zijn nog geen back-ups voor deze iPod.")
            return
        choice = dialogs.BackupDialog(self, backups).run()
        if choice:
            lib = self.lib
            self.run_task("Back-up terugzetten…", lambda r: lib.restore_backup(choice), lambda _: self.render_all())

    def open_settings(self):
        name = self.lib.device.name if self.lib else None
        detected = self.lib.device.firewire_guid() if self.lib else None
        if dialogs.SettingsDialog(self, self.cfg, name, detected).run() and self.lib:
            self.lib._guid_override = self.cfg.firewire_guids.get(name or "") or None

    # ================================================================== bibliotheek tonen

    def render_all(self):
        self.render_playlists()
        self.show_tracks()
        self.mark_dirty()
        if not self.lib:
            self.info_var.set("Geen iPod geopend")
            self.space["value"] = 0
            return
        dev = self.lib.device
        total, free = dev.disk_usage()
        self.space["value"] = 100 * (total - free) / total if total else 0
        self.info_var.set(
            f"{dev.model_name} · {len(self.lib.tracks)} nummers · {fmt_size(free)} vrij van {fmt_size(total)}"
        )

    def render_playlists(self):
        sel = self.pl_tree.selection()
        self.pl_tree.delete(*self.pl_tree.get_children())
        if not self.lib:
            return
        self.pl_tree.insert("", "end", iid=ALL_SONGS, text=f"♫  Alle nummers ({len(self.lib.tracks)})")
        for i, pl in enumerate(self.lib.playlists):
            icon = "⚙" if pl.is_smart else ("🎙" if pl.is_podcast else "≡")
            self.pl_tree.insert("", "end", iid=f"pl{i}", text=f"{icon}  {pl.name or '(naamloos)'} ({len(pl.track_ids)})")
        target = sel[0] if sel and self.pl_tree.exists(sel[0]) else ALL_SONGS
        self.pl_tree.selection_set(target)
        names = [p.name for p in self.lib.playlists if not p.is_smart and not p.is_podcast and p.name]
        self.target_pl_cb["values"] = [""] + names

    def current_playlist(self) -> Playlist | None:
        sel = self.pl_tree.selection()
        if not self.lib or not sel or sel[0] == ALL_SONGS:
            return None
        return self.lib.playlists[int(sel[0][2:])]

    def visible_tracks(self) -> list[Track]:
        if not self.lib:
            return []
        pl = self.current_playlist()
        tracks = self.lib.playlist_tracks(pl) if pl else self.lib.tracks
        q = self.search_var.get().strip().lower()
        if q:
            tracks = [t for t in tracks if q in " ".join(filter(None, (t.title, t.artist, t.album, t.genre))).lower()]
        col, rev = self._sort
        if col and not pl:
            key = {
                "#0": lambda t: (t.title or "").lower(),
                "artist": lambda t: ((t.artist or "").lower(), (t.album or "").lower(), t.cd_nr, t.track_nr),
                "album": lambda t: ((t.album or "").lower(), t.cd_nr, t.track_nr),
                "genre": lambda t: (t.genre or "").lower(),
                "time": lambda t: t.length_ms,
                "plays": lambda t: t.playcount,
            }[col]
            tracks = sorted(tracks, key=key, reverse=rev)
        return tracks

    def show_tracks(self):
        self.tree.delete(*self.tree.get_children())
        self._shown: dict[str, Track] = {}
        for i, t in enumerate(self.visible_tracks()):
            iid = str(i)
            self._shown[iid] = t
            self.tree.insert("", "end", iid=iid, text=t.title or "(geen titel)",
                             values=(t.artist or "", t.album or "", t.genre or "", fmt_duration(t.length_ms), t.playcount))
        self.count_var.set(f"{len(self._shown)} nummers")

    def sort_by(self, col):
        c, rev = self._sort
        self._sort = (col, not rev if c == col else False)
        self.show_tracks()

    def selected_tracks(self) -> list[Track]:
        return [self._shown[i] for i in self.tree.selection() if i in getattr(self, "_shown", {})]

    # ================================================================== bibliotheek wijzigen

    def _guard(self) -> bool:
        if not self.lib:
            messagebox.showinfo("Geen iPod", "Open eerst een iPod.")
            return False
        if self.busy:
            messagebox.showinfo("Even geduld", "Er loopt nog een taak.")
            return False
        return True

    def edit_tracks(self):
        tracks = self.selected_tracks()
        if not tracks or not self._guard():
            return
        changes = dialogs.TagEditor(self, tracks).run()
        if changes:
            try:
                for t in tracks:
                    self.lib.update_track(t, **changes)
            except LibraryError as e:
                messagebox.showerror("Bewerken mislukt", str(e))
            self.render_all()

    def delete_tracks(self):
        tracks = self.selected_tracks()
        if not tracks or not self._guard():
            return
        what = f"'{tracks[0].title}'" if len(tracks) == 1 else f"{len(tracks)} nummers"
        if messagebox.askyesno("Verwijderen", f"{what} van de iPod verwijderen?\n\n"
                               "Het bestand wordt bij 'Opslaan naar iPod' echt gewist."):
            self.lib.delete_tracks(tracks)
            self.render_all()

    def new_playlist(self):
        if not self._guard():
            return
        name = dialogs.ask_string(self, "Nieuwe playlist", "Naam:")
        if name:
            self.lib.create_playlist(name)
            self.render_all()

    def rename_playlist(self):
        pl = self.current_playlist()
        if not pl or not self._guard():
            return
        name = dialogs.ask_string(self, "Playlist hernoemen", "Nieuwe naam:", pl.name or "")
        if name:
            self.lib.rename_playlist(pl, name)
            self.render_all()

    def delete_playlist(self):
        pl = self.current_playlist()
        if not pl or not self._guard():
            return
        if messagebox.askyesno("Playlist verwijderen", f"Playlist '{pl.name}' verwijderen? De nummers blijven staan."):
            self.lib.delete_playlist(pl)
            self.pl_tree.selection_set(ALL_SONGS)
            self.render_all()

    def _playlist_by_name(self, name: str, create: bool = True) -> Playlist | None:
        for p in self.lib.playlists:
            if p.name == name and not p.is_smart and not p.is_podcast:
                return p
        return self.lib.create_playlist(name) if create else None

    def add_selection_to_playlist(self):
        tracks = self.selected_tracks()
        if not tracks or not self._guard():
            return
        names = [p.name for p in self.lib.playlists if not p.is_smart and not p.is_podcast and p.name]
        name = dialogs.choose_playlist(self, names)
        if name:
            try:
                self.lib.add_to_playlist(self._playlist_by_name(name), tracks)
            except LibraryError as e:
                messagebox.showerror("Mislukt", str(e))
            self.render_all()

    def remove_from_playlist(self):
        pl = self.current_playlist()
        tracks = self.selected_tracks()
        if not pl or not tracks or not self._guard():
            return
        self.lib.remove_from_playlist(pl, tracks)
        self.render_all()

    def move_in_playlist(self, delta: int):
        pl = self.current_playlist()
        tracks = self.selected_tracks()
        if not pl or len(tracks) != 1 or not self._guard():
            return
        self.lib.move_in_playlist(pl, tracks[0], delta)
        self.render_all()
        for iid, t in self._shown.items():
            if t is tracks[0]:
                self.tree.selection_set(iid)
                self.tree.see(iid)

    # ================================================================== toevoegen

    def choose_files(self):
        exts = " ".join(f"*{e}" for e in sorted(SUPPORTED_EXTENSIONS))
        files = filedialog.askopenfilenames(title="Muziek kiezen", filetypes=[("Audio", exts), ("Alles", "*")])
        self._add_local(Path(f) for f in files)

    def choose_folder(self):
        d = filedialog.askdirectory(title="Map met muziek kiezen")
        if d:
            files = sorted(p for p in Path(d).rglob("*") if p.suffix.lower() in SUPPORTED_EXTENSIONS and not p.name.startswith("."))
            self._add_local(files)

    def _add_local(self, paths):
        paths = list(paths)
        if not paths:
            return

        def work(report):
            items = []
            for n, p in enumerate(paths, 1):
                report(f"Lezen {n}/{len(paths)}: {p.name}", n / len(paths))
                try:
                    items.append(pipeline.file_item(p))
                except Exception as e:  # noqa: BLE001
                    from ..models import TrackInfo

                    items.append(pipeline.QueueItem(TrackInfo(title=p.name, source="file", local_path=p),
                                                    status=pipeline.FAILED, error=str(e)))
            return items

        self.run_task("Bestanden lezen…", work, self._append_queue)

    def resolve_input(self):
        text = self.input.get("1.0", "end")
        inputs = parse_input(text)
        if not inputs:
            return
        cfg = self.cfg

        def work(report):
            report("Links ophalen…", 0.05)
            items = pipeline.resolve(inputs, cfg)
            self._events.put(("call", self._append_queue, items))
            todo = [i for i in items if i.needs_match]
            for n, item in enumerate(todo, 1):
                report(f"Zoeken op YouTube {n}/{len(todo)}: {item.track.display}", n / max(len(todo), 1))
                try:
                    pipeline.find_match(item)
                except Exception as e:  # noqa: BLE001
                    item.status, item.error = pipeline.FAILED, str(e)
                self._events.put(("refresh_queue",))
            return None

        def done(_):
            self.input.delete("1.0", "end")
            self.render_queue()

        self.run_task("Ophalen…", work, done)

    def _append_queue(self, items):
        if items:
            self.queue_items.extend(items)
            self.render_queue()
            self.tabs.select(1)

    def render_queue(self):
        sel = set(self.qtree.selection())
        self.qtree.delete(*self.qtree.get_children())
        for i, item in enumerate(self.queue_items):
            t = item.track
            if t.source == "file" and not item.error:
                match = str(t.local_path)
            elif item.error:
                match = ("ℹ " if item.status == pipeline.SKIPPED else "⚠ ") + item.error
            elif t.youtube_title:
                match = t.youtube_title
            else:
                match = "—"
            iid = str(i)
            self.qtree.insert("", "end", iid=iid, text=t.display,
                              values=(item.status, {"spotify": "Spotify", "youtube": "YouTube", "file": "Bestand",
                                                    "text": "Tekst"}[t.source], match))
            if iid in sel:
                self.qtree.selection_add(iid)

    def selected_queue_items(self):
        return [self.queue_items[int(i)] for i in self.qtree.selection()]

    def remove_queue_items(self):
        if self.busy:
            return
        chosen = set(map(id, self.selected_queue_items()))
        self.queue_items = [q for q in self.queue_items if id(q) not in chosen]
        self.render_queue()

    def clear_queue(self):
        if self.busy:
            return
        self.queue_items = []
        self.render_queue()

    def choose_other_match(self):
        items = self.selected_queue_items()
        if len(items) != 1 or self.busy:
            return
        item = items[0]
        if item.track.source == "file":
            return
        if not item.candidates and item.track.source in ("spotify", "text"):
            try:
                from ..sources import matcher, youtube

                item.candidates = matcher.rank(youtube.search(matcher.search_query(item.track)), item.track)
            except Exception as e:  # noqa: BLE001
                messagebox.showerror("Zoeken mislukt", str(e))
                return
        c = dialogs.MatchChooser(self, item.track.display, item.candidates).run()
        if c:
            pipeline.choose_candidate(item, c)
            item.error = ""
            self.render_queue()

    def add_queue_to_ipod(self):
        if not self._guard():
            return
        todo = [q for q in self.queue_items if q.status in (pipeline.MATCHED, pipeline.READY)]
        if not todo:
            messagebox.showinfo("Niets te doen", "Er staan geen nummers klaar. Haal eerst links of bestanden op.")
            return
        lib, cfg = self.lib, self.cfg
        target_name = self.target_pl.get().strip()
        workdir = download_dir() if cfg.keep_downloads else self._workdir
        free = lib.device.disk_usage()[1]

        def work(report):
            added: list[Track] = []
            needed = 0
            for n, item in enumerate(todo, 1):
                report(f"Voorbereiden {n}/{len(todo)}: {item.track.display}", (n - 1) / len(todo))
                try:
                    dup = lib.find_duplicate(item.track.title, item.track.artist, item.track.duration_ms)
                    if dup is not None:
                        item.status, item.error = pipeline.SKIPPED, f"staat al op de iPod: {dup.title}"
                        self._events.put(("refresh_queue",))
                        continue
                    path = item.prepared_path or pipeline.prepare(item, cfg, workdir)
                    needed += path.stat().st_size
                    if needed > free - 20 * 1024 * 1024:
                        raise RuntimeError("Niet genoeg ruimte op de iPod")
                    overrides = {} if item.use_file_tags else item.track.tag_overrides()
                    added.append(lib.add_file(path, overrides))
                    item.status, item.error = pipeline.ADDED, ""
                except Exception as e:  # noqa: BLE001
                    item.status, item.error = pipeline.FAILED, str(e)
                self._events.put(("refresh_queue",))
            if added and target_name:
                pl = next((p for p in lib.playlists if p.name == target_name), None) or lib.create_playlist(target_name)
                lib.add_to_playlist(pl, added)
            if added:
                report("Opslaan naar iPod…", 0.95)
                lib.save(progress=report)
            return len(added)

        def done(count):
            self.render_queue()
            self.render_all()
            failed = sum(q.status == pipeline.FAILED for q in todo)
            skipped = sum(q.status == pipeline.SKIPPED for q in todo)
            msg = f"{count} nummer(s) op de iPod gezet."
            if skipped:
                msg += f"\n{skipped} overgeslagen (stonden er al op)."
            if failed:
                msg += f"\n{failed} mislukt. Zie de kolom 'Gevonden audio' voor de reden."
            messagebox.showinfo("Klaar", msg + "\n\nKlik op 'Uitwerpen' voordat je de iPod loskoppelt.")

        self.run_task("Nummers toevoegen…", work, done)

    # ================================================================== afsluiten

    def on_close(self):
        if self.busy and not messagebox.askyesno("Bezig", "Er loopt nog een taak. Toch afsluiten?"):
            return
        if self.lib and self.lib.dirty and not self._confirm_discard():
            return
        shutil.rmtree(self._workdir, ignore_errors=True)
        self.destroy()


def main():
    App().mainloop()
