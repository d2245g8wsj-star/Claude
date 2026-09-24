/* Hulpprogramma voor tests: maakt met libgpod een nep-iPod (nano 3G) en leest
 * een iTunesDB terug. Alleen nodig om ipodsync te verifieren, niet voor de app.
 *
 *   gpodtool make <mountpoint> <firewire_guid> <aantal_tracks>
 *   gpodtool ls   <mountpoint>
 */
#include <gpod/itdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int do_make(const char *mp, const char *guid, int n)
{
    GError *err = NULL;
    if (!itdb_init_ipod(mp, "MA978", "Test iPod", &err)) {
        fprintf(stderr, "init: %s\n", err ? err->message : "?");
        return 1;
    }
    Itdb_iTunesDB *db = itdb_parse(mp, &err);
    if (!db) { fprintf(stderr, "parse: %s\n", err->message); return 1; }
    itdb_device_set_sysinfo(db->device, "FirewireGuid", guid);
    Itdb_Playlist *mpl = itdb_playlist_mpl(db);
    Itdb_Playlist *pl = itdb_playlist_new("Favorieten", FALSE);
    itdb_playlist_add(db, pl, -1);
    const char *artists[] = {"The Beatles", "Brooke Fraser", "Ólafur Arnalds", "ABBA"};
    for (int i = 0; i < n; i++) {
        Itdb_Track *t = itdb_track_new();
        char buf[64];
        snprintf(buf, sizeof buf, "Nummer %d", i + 1);
        t->title = g_strdup(buf);
        t->artist = g_strdup(artists[i % 4]);
        snprintf(buf, sizeof buf, "Album %d", i % 3);
        t->album = g_strdup(buf);
        t->genre = g_strdup("Pop");
        t->filetype = g_strdup("MPEG audio file");
        snprintf(buf, sizeof buf, ":iPod_Control:Music:F0%d:TEST%d.mp3", i % 5, i);
        t->ipod_path = g_strdup(buf);
        t->size = 1000 + i; t->tracklen = 180000 + i; t->track_nr = i + 1;
        t->bitrate = 192; t->samplerate = 44100; t->mediatype = ITDB_MEDIATYPE_AUDIO;
        t->playcount = i;
        itdb_track_add(db, t, -1);
        itdb_playlist_add_track(mpl, t, -1);
        if (i % 2 == 0) itdb_playlist_add_track(pl, t, -1);
    }
    if (!itdb_write(db, &err)) { fprintf(stderr, "write: %s\n", err->message); return 1; }
    itdb_free(db);
    return 0;
}

static int do_ls(const char *mp)
{
    GError *err = NULL;
    Itdb_iTunesDB *db = itdb_parse(mp, &err);
    if (!db) { fprintf(stderr, "parse: %s\n", err->message); return 1; }
    printf("tracks %u\n", itdb_tracks_number(db));
    for (GList *l = db->tracks; l; l = l->next) {
        Itdb_Track *t = l->data;
        printf("T|%u|%s|%s|%s|%s|%s|%u|%u\n", t->id, t->title ?: "", t->artist ?: "",
               t->album ?: "", t->genre ?: "", t->ipod_path ?: "", t->size, t->tracklen);
    }
    for (GList *l = db->playlists; l; l = l->next) {
        Itdb_Playlist *p = l->data;
        printf("P|%s|%d|%u\n", p->name, itdb_playlist_is_mpl(p), itdb_playlist_tracks_number(p));
        for (GList *m = p->members; m; m = m->next)
            printf("M|%s\n", ((Itdb_Track *)m->data)->title ?: "");
    }
    itdb_free(db);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "make")) return do_make(argv[2], argv[3], atoi(argv[4]));
    if (argc >= 3 && !strcmp(argv[1], "ls")) return do_ls(argv[2]);
    fprintf(stderr, "gebruik: gpodtool make <mp> <guid> <n> | gpodtool ls <mp>\n");
    return 2;
}
