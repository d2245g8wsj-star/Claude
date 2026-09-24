import json
from pathlib import Path

from mutagen.mp4 import MP4

from ipodsync.config import Config
from ipodsync.models import TrackInfo
from ipodsync.sources import pipeline, spotify, youtube
from ipodsync.sources.matcher import Candidate, best_match, rank, search_query
from ipodsync.sources.parse import clean_video_title, parse_input, split_artist_title
from ipodsync.tagger import square_jpeg, write_tags


def test_parse_input(tmp_path):
    f = tmp_path / "eigen.mp3"
    f.write_bytes(b"")
    items = parse_input(
        "https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M?si=abc\n"
        "spotify:track:4uLU6hMCjMI75M1A2tKUQC\n"
        "https://open.spotify.com/intl-nl/album/1DFixLWuPkv3KT3TnV35m3\n"
        "https://www.youtube.com/watch?v=dQw4w9WgXcQ\n"
        "https://music.youtube.com/playlist?list=PL123\n"
        f"{f}\n"
        "\n# commentaar\n"
        "Brooke Fraser - Albertine\n"
    )
    assert [(i.kind, i.spotify_type) for i in items] == [
        ("spotify", "playlist"), ("spotify", "track"), ("spotify", "album"),
        ("youtube", ""), ("youtube", ""), ("file", ""), ("text", ""),
    ]
    assert items[0].spotify_id == "37i9dQZF1DXcBWIGoYBM5M"


def test_split_and_clean():
    assert split_artist_title("Brooke Fraser - Albertine") == ("Brooke Fraser", "Albertine")
    assert split_artist_title("Albertine") == ("", "Albertine")
    assert clean_video_title("Brooke Fraser - Albertine (Official Video)") == "Brooke Fraser - Albertine"
    assert clean_video_title("Song [Lyrics] (HD)") == "Song"
    assert clean_video_title("Song (Live at Wembley)") == "Song (Live at Wembley)"


def _target(**kw):
    return TrackInfo(title="Albertine", artists=["Brooke Fraser"], duration_ms=250_000, **kw)


def test_matcher_prefers_studio_version_with_right_length():
    cands = [
        Candidate("u1", "Brooke Fraser - Albertine (Live)", "Brooke Fraser", 262),
        Candidate("u2", "Albertine", "Brooke Fraser - Topic", 251),
        Candidate("u3", "Albertine - Brooke Fraser (cover)", "Someone", 249),
        Candidate("u4", "Brooke Fraser - Albertine (Official Video)", "BrookeFraserVEVO", 280),
    ]
    assert rank(cands, _target())[0].url == "u2"
    assert best_match(cands, _target()).url == "u2"


def test_matcher_keeps_live_if_requested():
    t = TrackInfo(title="Albertine (Live)", artists=["Brooke Fraser"], duration_ms=262_000)
    cands = [
        Candidate("studio", "Albertine", "Brooke Fraser - Topic", 251),
        Candidate("live", "Brooke Fraser - Albertine (Live)", "Brooke Fraser", 262),
    ]
    assert best_match(cands, t).url == "live"


def test_matcher_rejects_garbage():
    cands = [Candidate("x", "Totally different song", "Other", 90)]
    assert best_match(cands, _target()) is None
    assert search_query(_target()) == "Brooke Fraser - Albertine audio"


def test_spotify_api_track_mapping():
    album = {
        "name": "Albertine", "total_tracks": 11, "release_date": "2006-10-02",
        "images": [{"url": "https://i.scdn.co/big.jpg"}], "artists": [{"name": "Brooke Fraser"}],
    }
    t = spotify._track_from_api(
        {"name": "Albertine", "artists": [{"name": "Brooke Fraser"}], "track_number": 4,
         "disc_number": 1, "duration_ms": 250000,
         "external_urls": {"spotify": "https://open.spotify.com/track/x"}},
        album,
    )
    assert (t.title, t.artist, t.album, t.albumartist, t.year, t.track_nr, t.tracks_total) == (
        "Albertine", "Brooke Fraser", "Albertine", "Brooke Fraser", 2006, 4, 11,
    )
    assert t.cover_url == "https://i.scdn.co/big.jpg"


def test_spotify_embed_parsing():
    data = {"props": {"pageProps": {"state": {"data": {"entity": {
        "name": "Mijn playlist",
        "trackList": [
            {"uri": "spotify:track:aaa", "title": "Albertine", "subtitle": "Brooke Fraser", "duration": 250000},
            {"uri": "spotify:track:bbb", "title": "Duet", "subtitle": "A, B", "duration": 180000},
        ],
    }}}}}}
    html = f'<html><script id="__NEXT_DATA__" type="application/json">{json.dumps(data)}</script></html>'
    name, tracks = spotify.parse_embed_html(html, "playlist")
    assert name == "Mijn playlist"
    assert [(t.title, t.artists, t.duration_ms) for t in tracks] == [
        ("Albertine", ["Brooke Fraser"], 250000), ("Duet", ["A", "B"], 180000),
    ]
    assert tracks[0].source_url == "https://open.spotify.com/track/aaa"


def test_youtube_info_mapping():
    t = youtube.info_to_track({
        "title": "Brooke Fraser - Albertine (Official Video)", "channel": "BrookeFraserVEVO",
        "duration": 250, "webpage_url": "https://youtu.be/x", "thumbnail": "https://img",
    })
    assert (t.title, t.artists, t.duration_ms, t.youtube_url) == ("Albertine", ["Brooke Fraser"], 250000, "https://youtu.be/x")
    t = youtube.info_to_track({
        "title": "Albertine", "track": "Albertine", "artists": ["Brooke Fraser"], "album": "Albertine",
        "release_year": 2006, "channel": "Brooke Fraser - Topic", "duration": 250, "webpage_url": "u",
    })
    assert (t.title, t.artist, t.album, t.year) == ("Albertine", "Brooke Fraser", "Albertine", 2006)
    t = youtube.info_to_track({"title": "Albertine", "channel": "Brooke Fraser - Topic", "duration": 1, "url": "u"})
    assert t.artists == ["Brooke Fraser"]


def test_tagger_writes_mp4_tags_and_cover(tmp_path, flac_file):
    from io import BytesIO

    from PIL import Image

    from ipodsync.audio import convert_to_aac, probe

    m4a = convert_to_aac(flac_file, tmp_path)
    img = BytesIO()
    Image.new("RGB", (1280, 720), "red").save(img, "PNG")
    cover = square_jpeg(img.getvalue())
    t = TrackInfo(title="Albertine", artists=["Brooke Fraser"], album="Albertine", year=2006, track_nr=4, tracks_total=11)
    write_tags(m4a, t, cover)
    info = probe(m4a)
    assert info.tags["title"] == "Albertine" and info.tags["artist"] == "Brooke Fraser"
    assert info.tags["track_nr"] == "4" and info.tags["year"] == "2006"
    covr = MP4(m4a).tags["covr"][0]
    assert Image.open(BytesIO(bytes(covr))).size == (300, 300)


def test_pipeline_with_fake_network(tmp_path, monkeypatch, flac_file):
    from ipodsync.audio import convert_to_aac
    from ipodsync.sources.parse import parse_input

    m4a = convert_to_aac(flac_file, tmp_path)
    monkeypatch.setattr(spotify, "fetch", lambda *a: ("PL", [_target(album="Albertine")]))
    monkeypatch.setattr(youtube, "search", lambda q, n=6: [
        Candidate("https://yt/good", "Albertine", "Brooke Fraser - Topic", 250),
        Candidate("https://yt/bad", "Albertine (Live)", "x", 300),
    ])

    def fake_download(url, dest, bitrate="256k"):
        out = dest / "dl.m4a"
        out.write_bytes(m4a.read_bytes())
        return out, {"thumbnail": ""}

    monkeypatch.setattr(youtube, "download", fake_download)
    items = pipeline.resolve(parse_input("https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M"), Config())
    assert len(items) == 1 and items[0].needs_match
    pipeline.find_match(items[0])
    assert items[0].status == pipeline.MATCHED and items[0].track.youtube_url == "https://yt/good"
    path = pipeline.prepare(items[0], Config(), tmp_path / "work")
    assert path.exists() and items[0].status == pipeline.READY
    assert MP4(path).tags["\xa9alb"] == ["Albertine"]
