"""Metadata van Spotify-tracks, -albums en -playlists ophalen.

Er wordt géén audio van Spotify gedownload (die heeft DRM). We halen alleen titel,
artiest, album, duur en hoes op, om daarmee de audio op YouTube te zoeken.

Twee manieren:
1. De officiële Web API (met eigen, gratis Client ID/Secret): volledig en betrouwbaar.
2. Zonder sleutels: de openbare "embed"-pagina. Die geeft minder metadata (geen
   album) en werkt tot ongeveer 100 nummers.
"""

from __future__ import annotations

import json
import re
import urllib.request

from ..models import TrackInfo


class SpotifyError(Exception):
    pass


def fetch(kind: str, item_id: str, client_id: str = "", client_secret: str = "") -> tuple[str, list[TrackInfo]]:
    """Geeft (naam van playlist/album/track, lijst nummers)."""
    if client_id and client_secret:
        return _fetch_api(kind, item_id, client_id, client_secret)
    return _fetch_embed(kind, item_id)


# ----------------------------------------------------------------- Web API


def _track_from_api(t: dict, album: dict | None = None) -> TrackInfo:
    album = album or t.get("album") or {}
    images = album.get("images") or []
    release = str(album.get("release_date") or "")
    album_artists = [a["name"] for a in album.get("artists", []) if a.get("name")]
    url = (t.get("external_urls") or {}).get("spotify", "")
    return TrackInfo(
        title=t.get("name", ""),
        artists=[a["name"] for a in t.get("artists", []) if a.get("name")],
        album=album.get("name", ""),
        albumartist=album_artists[0] if album_artists else "",
        track_nr=int(t.get("track_number") or 0),
        tracks_total=int(album.get("total_tracks") or 0),
        cd_nr=int(t.get("disc_number") or 0),
        year=int(release[:4]) if release[:4].isdigit() else 0,
        duration_ms=int(t.get("duration_ms") or 0),
        cover_url=images[0]["url"] if images else "",
        source="spotify",
        source_url=url,
    )


def _fetch_api(kind: str, item_id: str, client_id: str, client_secret: str) -> tuple[str, list[TrackInfo]]:
    import spotipy
    from spotipy.oauth2 import SpotifyClientCredentials

    sp = spotipy.Spotify(
        auth_manager=SpotifyClientCredentials(client_id=client_id, client_secret=client_secret),
        requests_timeout=20,
        retries=3,
    )
    try:
        if kind == "track":
            t = sp.track(item_id)
            return t["name"], [_track_from_api(t)]
        if kind == "album":
            album = sp.album(item_id)
            tracks = []
            page = album["tracks"]
            while page:
                tracks += [_track_from_api(t, album) for t in page["items"] if t]
                page = sp.next(page) if page.get("next") else None
            return album["name"], tracks
        if kind == "playlist":
            meta = sp.playlist(item_id, fields="name")
            tracks = []
            page = sp.playlist_items(item_id, additional_types=("track",))
            while page:
                for it in page["items"]:
                    t = (it or {}).get("track")
                    if t and t.get("type", "track") == "track" and not t.get("is_local"):
                        tracks.append(_track_from_api(t))
                page = sp.next(page) if page.get("next") else None
            return meta["name"], tracks
    except spotipy.SpotifyException as e:
        if e.http_status == 404:
            raise SpotifyError(
                "Spotify geeft 'niet gevonden'. Privé-playlists en playlists die Spotify zelf "
                "maakt (bijv. 'Mix van de dag') zijn niet via de API op te vragen."
            ) from e
        raise SpotifyError(f"Spotify-fout: {e.msg}") from e
    raise SpotifyError(f"Onbekend Spotify-type: {kind}")


# ----------------------------------------------------------------- embed (zonder sleutels)


def _fetch_embed(kind: str, item_id: str) -> tuple[str, list[TrackInfo]]:
    url = f"https://open.spotify.com/embed/{kind}/{item_id}"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (Macintosh) ipodsync"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            html = r.read().decode("utf-8", "replace")
    except Exception as e:
        raise SpotifyError(f"Kon Spotify niet bereiken: {e}") from e
    return parse_embed_html(html, kind, item_id)


def parse_embed_html(html: str, kind: str = "playlist", item_id: str = "") -> tuple[str, list[TrackInfo]]:
    m = re.search(r'<script[^>]*id="__NEXT_DATA__"[^>]*>(.*?)</script>', html, re.S)
    if not m:
        raise SpotifyError(
            "Kon de Spotify-pagina niet lezen. Vul een Spotify Client ID/Secret in bij Instellingen."
        )
    data = json.loads(m.group(1))
    entity = _find_entity(data)
    if entity is None:
        raise SpotifyError("Onverwacht formaat van de Spotify-pagina")
    name = entity.get("name") or entity.get("title") or ""
    cover = _cover(entity)
    tracks: list[TrackInfo] = []
    if kind == "track" or not entity.get("trackList"):
        artists = [a.get("name", "") for a in entity.get("artists", []) if a.get("name")]
        tracks.append(
            TrackInfo(
                title=name,
                artists=artists or _split_artists(entity.get("subtitle", "")),
                duration_ms=int(entity.get("duration") or 0),
                cover_url=cover,
                source="spotify",
                source_url=f"https://open.spotify.com/track/{item_id}" if item_id else "",
            )
        )
        return name, tracks
    for i, t in enumerate(entity["trackList"], 1):
        uri = t.get("uri", "")
        tracks.append(
            TrackInfo(
                title=t.get("title", ""),
                artists=_split_artists(t.get("subtitle", "")),
                album=name if kind == "album" else "",
                track_nr=i if kind == "album" else 0,
                duration_ms=int(t.get("duration") or 0),
                cover_url=cover if kind == "album" else "",
                source="spotify",
                source_url="https://open.spotify.com/track/" + uri.rsplit(":", 1)[-1] if uri else "",
            )
        )
    return name, tracks


def _find_entity(obj):
    if isinstance(obj, dict):
        if "entity" in obj and isinstance(obj["entity"], dict):
            return obj["entity"]
        for v in obj.values():
            found = _find_entity(v)
            if found is not None:
                return found
    elif isinstance(obj, list):
        for v in obj:
            found = _find_entity(v)
            if found is not None:
                return found
    return None


def _cover(entity: dict) -> str:
    for key in ("coverArt", "visualIdentity"):
        sources = (entity.get(key) or {}).get("sources") or (entity.get(key) or {}).get("image") or []
        if sources:
            best = max(sources, key=lambda s: s.get("width") or s.get("maxWidth") or 0)
            return best.get("url", "")
    return ""


def _split_artists(subtitle: str) -> list[str]:
    parts = [p.strip() for p in re.split(r",\s*| ?• ?", subtitle) if p.strip()]
    return parts
