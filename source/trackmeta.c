#include "trackmeta.h"
#include "path_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

/* --- small helpers -------------------------------------------------------- */

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t syncsafe32(const uint8_t *p)
{
    return ((uint32_t)(p[0] & 0x7f) << 21) | ((uint32_t)(p[1] & 0x7f) << 14)
         | ((uint32_t)(p[2] & 0x7f) << 7) | (uint32_t)(p[3] & 0x7f);
}

static void utf8_push(char *out, size_t out_sz, size_t *o, uint32_t cp)
{
    if (cp < 0x80) {
        if (*o + 1 < out_sz)
            out[(*o)++] = (char)cp;
    } else if (cp < 0x800) {
        if (*o + 2 < out_sz) {
            out[(*o)++] = (char)(0xC0 | (cp >> 6));
            out[(*o)++] = (char)(0x80 | (cp & 0x3F));
        }
    } else if (cp < 0x10000) {
        if (*o + 3 < out_sz) {
            out[(*o)++] = (char)(0xE0 | (cp >> 12));
            out[(*o)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[(*o)++] = (char)(0x80 | (cp & 0x3F));
        }
    } else {
        if (*o + 4 < out_sz) {
            out[(*o)++] = (char)(0xF0 | (cp >> 18));
            out[(*o)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[(*o)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[(*o)++] = (char)(0x80 | (cp & 0x3F));
        }
    }
}

/* Convert an ID3v2 text payload (encoding byte semantics) to UTF-8. */
static void id3_text_to_utf8(int enc, const uint8_t *p, size_t n,
                             char *out, size_t out_sz)
{
    size_t o = 0;

    out[0] = '\0';
    if (enc == 1 || enc == 2) { /* UTF-16 with BOM / UTF-16BE */
        int be = (enc == 2);
        size_t i = 0;

        if (enc == 1 && n >= 2) {
            if (p[0] == 0xFE && p[1] == 0xFF) { be = 1; i = 2; }
            else if (p[0] == 0xFF && p[1] == 0xFE) { be = 0; i = 2; }
        }
        while (i + 1 < n) {
            uint32_t u = be ? ((uint32_t)p[i] << 8) | p[i + 1]
                            : ((uint32_t)p[i + 1] << 8) | p[i];
            i += 2;
            if (u == 0)
                break;
            if (u >= 0xD800 && u <= 0xDBFF && i + 1 < n) {
                uint32_t lo = be ? ((uint32_t)p[i] << 8) | p[i + 1]
                                 : ((uint32_t)p[i + 1] << 8) | p[i];
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    u = 0x10000 + (((u - 0xD800) << 10) | (lo - 0xDC00));
                    i += 2;
                }
            }
            utf8_push(out, out_sz, &o, u);
        }
    } else if (enc == 3) { /* UTF-8 */
        size_t i;
        for (i = 0; i < n && p[i] != 0; i++) {
            if (o + 1 < out_sz)
                out[o++] = (char)p[i];
        }
    } else { /* ISO-8859-1 */
        size_t i;
        for (i = 0; i < n && p[i] != 0; i++)
            utf8_push(out, out_sz, &o, p[i]);
    }
    out[o] = '\0';

    /* trim trailing spaces */
    while (o > 0 && out[o - 1] == ' ')
        out[--o] = '\0';
}

/* "(17)Rock" -> "Rock"; bare "(17)"/"17" stays as-is. */
static void strip_genre_paren(char *s)
{
    if (s[0] == '(') {
        char *close = strchr(s, ')');
        if (close != NULL && close[1] != '\0')
            memmove(s, close + 1, strlen(close + 1) + 1);
    }
}

static void take_art(trackmeta_t *m, const uint8_t *data, size_t size)
{
    uint8_t *copy;

    if (size == 0 || size > TRACKMETA_ART_MAX)
        return;
    copy = malloc(size);
    if (copy == NULL)
        return;
    memcpy(copy, data, size);
    free(m->art);
    m->art      = copy;
    m->art_size = size;
}

/* --- ID3v2 (MP3) ----------------------------------------------------------
 * Frames are streamed one at a time: header from the file, then the body is
 * loaded only when the frame is one we display (text frames are small, the
 * picture frame is capped at TRACKMETA_ART_MAX). */

/* Remove ID3 unsynchronisation (0x00 stuffed after every 0xFF) in place. */
static size_t de_unsync(uint8_t *p, size_t n)
{
    size_t r, w = 0;

    for (r = 0; r < n; r++) {
        p[w++] = p[r];
        if (p[r] == 0xFF && r + 1 < n && p[r + 1] == 0x00)
            r++;
    }
    return w;
}

static void id3_handle_text(trackmeta_t *m, const char *id,
                            const uint8_t *body, size_t n)
{
    char *dst    = NULL;
    size_t dst_sz = 0;

    if (n < 2)
        return;
    if (!strcmp(id, "TIT2") || !strcmp(id, "TT2")) {
        dst = m->title;  dst_sz = sizeof(m->title);
    } else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1")) {
        dst = m->artist; dst_sz = sizeof(m->artist);
    } else if (!strcmp(id, "TALB") || !strcmp(id, "TAL")) {
        dst = m->album;  dst_sz = sizeof(m->album);
    } else if (!strcmp(id, "TCON") || !strcmp(id, "TCO")) {
        dst = m->genre;  dst_sz = sizeof(m->genre);
    } else if (!strcmp(id, "TYER") || !strcmp(id, "TDRC") || !strcmp(id, "TYE")) {
        dst = m->year;   dst_sz = sizeof(m->year);
    } else if (!strcmp(id, "TRCK") || !strcmp(id, "TRK")) {
        dst = m->track;  dst_sz = sizeof(m->track);
    } else {
        return;
    }

    id3_text_to_utf8(body[0], body + 1, n - 1, dst, dst_sz);
    if (dst == m->genre)
        strip_genre_paren(dst);
    if (dst == m->year && strlen(dst) > 4)
        dst[4] = '\0'; /* TDRC is a full timestamp; keep the year */
}

/* APIC (v2.3/2.4) or PIC (v2.2): find the start of the image data. */
static void id3_handle_pic(trackmeta_t *m, int v22, uint8_t *body, size_t n,
                           int *have_front)
{
    size_t i = 0;
    int    enc, ptype;

    if (n < 4)
        return;
    enc = body[i++];
    if (v22) {
        i += 3; /* image format, e.g. "JPG" */
    } else {
        while (i < n && body[i] != 0)
            i++; /* MIME (latin1) */
        i++;
    }
    if (i >= n)
        return;
    ptype = body[i++];

    /* description, terminator depends on encoding */
    if (enc == 1 || enc == 2) {
        while (i + 1 < n && (body[i] != 0 || body[i + 1] != 0))
            i += 2;
        i += 2;
    } else {
        while (i < n && body[i] != 0)
            i++;
        i++;
    }
    if (i >= n)
        return;

    if (m->art != NULL && (*have_front || ptype != 3))
        return; /* keep the front cover once we have one */
    take_art(m, body + i, n - i);
    if (m->art != NULL && ptype == 3)
        *have_front = 1;
}

static void read_id3v2(FILE *f, trackmeta_t *m)
{
    uint8_t  hdr[10];
    int      ver, v22;
    long     tag_end;
    int      have_front = 0;

    if (fread(hdr, 1, 10, f) != 10 || memcmp(hdr, "ID3", 3) != 0)
        return;
    ver = hdr[3];
    if (ver < 2 || ver > 4)
        return;
    v22     = (ver == 2);
    tag_end = 10 + (long)syncsafe32(hdr + 6);

    if (hdr[5] & 0x40) { /* extended header: skip it */
        uint8_t xh[4];
        if (fread(xh, 1, 4, f) != 4)
            return;
        if (fseek(f, (long)(ver == 4 ? syncsafe32(xh) - 4 : be32(xh)),
                  SEEK_CUR) != 0)
            return;
    }

    while (ftell(f) + (v22 ? 6 : 10) <= tag_end) {
        char     id[5] = {0};
        uint8_t  fh[10];
        uint32_t fsize;
        int      flags = 0;
        size_t   hlen  = v22 ? 6 : 10;

        if (fread(fh, 1, hlen, f) != hlen)
            break;
        if (fh[0] == 0)
            break; /* padding */

        if (v22) {
            memcpy(id, fh, 3);
            fsize = ((uint32_t)fh[3] << 16) | ((uint32_t)fh[4] << 8) | fh[5];
        } else {
            memcpy(id, fh, 4);
            fsize = (ver == 4) ? syncsafe32(fh + 4) : be32(fh + 4);
            flags = fh[9];
        }
        if (fsize == 0 || (long)fsize > tag_end - ftell(f))
            break;

        /* Only frames we display are read; compressed/encrypted are not
         * (v2.3 keeps those bits in 0xC0, v2.4 moved them to 0x0C). */
        if ((!v22 && (flags & (ver == 4 ? 0x0C : 0xC0)))
            || (id[0] != 'T' && strcmp(id, "APIC") != 0 && strcmp(id, "PIC") != 0)
            || fsize > TRACKMETA_ART_MAX) {
            if (fseek(f, (long)fsize, SEEK_CUR) != 0)
                break;
            continue;
        }

        {
            uint8_t *body = malloc(fsize);
            size_t   n;

            if (body == NULL)
                break;
            if (fread(body, 1, fsize, f) != fsize) {
                free(body);
                break;
            }
            n = fsize;
            if (!v22 && ver == 4 && (flags & 0x02))
                n = de_unsync(body, n);
            if (ver == 4 && (flags & 0x01) && n > 4) { /* data length indicator */
                memmove(body, body + 4, n - 4);
                n -= 4;
            }

            if (id[0] == 'T')
                id3_handle_text(m, id, body, n);
            else
                id3_handle_pic(m, v22, body, n, &have_front);
            free(body);
        }
    }
}

/* --- FLAC ----------------------------------------------------------------- */

static void flac_handle_comment(trackmeta_t *m, const char *kv, size_t n)
{
    struct { const char *key; char *dst; size_t sz; } map[] = {
        { "TITLE=",       m->title,  sizeof(m->title)  },
        { "ARTIST=",      m->artist, sizeof(m->artist) },
        { "ALBUM=",       m->album,  sizeof(m->album)  },
        { "GENRE=",       m->genre,  sizeof(m->genre)  },
        { "DATE=",        m->year,   sizeof(m->year)   },
        { "TRACKNUMBER=", m->track,  sizeof(m->track)  },
    };
    size_t i;

    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        size_t klen = strlen(map[i].key);
        if (n > klen && strncasecmp(kv, map[i].key, klen) == 0) {
            size_t vlen = n - klen;
            if (vlen >= map[i].sz)
                vlen = map[i].sz - 1;
            memcpy(map[i].dst, kv + klen, vlen);
            map[i].dst[vlen] = '\0';
            if (map[i].dst == m->year && vlen > 4)
                map[i].dst[4] = '\0';
            return;
        }
    }
}

static void read_flac_vorbis(trackmeta_t *m, const uint8_t *b, size_t n)
{
    size_t   i = 0;
    uint32_t vend, cnt, k;

    if (n < 8)
        return;
    vend = le32(b);
    i    = 4 + vend;
    if (i + 4 > n)
        return;
    cnt = le32(b + i);
    i += 4;
    for (k = 0; k < cnt && i + 4 <= n; k++) {
        uint32_t len = le32(b + i);
        i += 4;
        if (len > n - i)
            break;
        flac_handle_comment(m, (const char *)(b + i), len);
        i += len;
    }
}

static void read_flac_picture(trackmeta_t *m, const uint8_t *b, size_t n,
                              int *have_front)
{
    size_t   i = 0;
    uint32_t ptype, len;

    if (n < 8)
        return;
    ptype = be32(b);
    i = 4;
    len = be32(b + i); /* MIME */
    i += 4 + len;
    if (i + 4 > n)
        return;
    len = be32(b + i); /* description */
    i += 4 + len;
    i += 16;           /* width/height/depth/colors */
    if (i + 4 > n)
        return;
    len = be32(b + i); /* picture data */
    i += 4;
    if (len > n - i)
        return;

    if (m->art != NULL && (*have_front || ptype != 3))
        return;
    take_art(m, b + i, len);
    if (m->art != NULL && ptype == 3)
        *have_front = 1;
}

static void read_flac(FILE *f, trackmeta_t *m)
{
    uint8_t magic[4];
    int     have_front = 0;
    int     last = 0;

    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "fLaC", 4) != 0)
        return;

    while (!last) {
        uint8_t  bh[4];
        uint32_t type, size;

        if (fread(bh, 1, 4, f) != 4)
            break;
        last = (bh[0] & 0x80) != 0;
        type = bh[0] & 0x7f;
        size = ((uint32_t)bh[1] << 16) | ((uint32_t)bh[2] << 8) | bh[3];

        if ((type == 4 || type == 6) && size <= TRACKMETA_ART_MAX) {
            uint8_t *body = malloc(size);
            if (body == NULL)
                break;
            if (fread(body, 1, size, f) != size) {
                free(body);
                break;
            }
            if (type == 4)
                read_flac_vorbis(m, body, size);
            else
                read_flac_picture(m, body, size, &have_front);
            free(body);
        } else if (fseek(f, (long)size, SEEK_CUR) != 0) {
            break;
        }
    }
}

/* --- Opus (Ogg OpusTags) -------------------------------------------------- */

static void read_opus_tags(FILE *f, trackmeta_t *m)
{
    unsigned char buf[8192];
    size_t n;
    size_t i;

    n = fread(buf, 1, sizeof(buf), f);
    if (n < 16 || memcmp(buf, "OggS", 4) != 0)
        return;

    for (i = 0; i + 8 <= n; i++) {
        size_t vend, cnt, k, pos;

        if (memcmp(buf + i, "OpusTags", 8) != 0)
            continue;
        pos = i + 8;
        if (pos + 4 > n)
            return;
        vend = le32(buf + pos);
        pos += 4 + vend;
        if (pos + 4 > n)
            return;
        cnt = le32(buf + pos);
        pos += 4;
        for (k = 0; k < cnt && pos + 4 <= n; k++) {
            uint32_t len = le32(buf + pos);
            pos += 4;
            if (len > n - pos)
                return;
            flac_handle_comment(m, (const char *)(buf + pos), len);
            pos += len;
        }
        return;
    }
}

/* --- public API ------------------------------------------------------------ */

int trackmeta_read(const char *path, trackmeta_t *m)
{
    FILE *f;

    memset(m, 0, sizeof(*m));
    if (path == NULL)
        return -1;
    f = fopen(path, "rb");
    if (f == NULL)
        return -1;

    if (path_is_flac(path))
        read_flac(f, m);
    else if (path_is_opus(path))
        read_opus_tags(f, m);
    else
        read_id3v2(f, m);

    fclose(f);
    return 0;
}

void trackmeta_free(trackmeta_t *m)
{
    free(m->art);
    m->art      = NULL;
    m->art_size = 0;
}

/* Covers above this many pixels are skipped: decoding happens while the
 * next track's playback/decode threads are allocating, and a huge decode
 * can transiently exhaust the heap and make those thread creations fail. */
#define ART_MAX_PIXELS (1600 * 1600)

int trackmeta_decode_art(const uint8_t *data, size_t size,
                         uint16_t *out, int w, int h)
{
    int      sw, sh, comp;
    uint8_t *px;
    int      dx, dy;

    if (data == NULL || size == 0 || w <= 0 || h <= 0)
        return -1;
    if (!stbi_info_from_memory(data, (int)size, &sw, &sh, &comp)
        || sw <= 0 || sh <= 0
        || (long)sw * (long)sh > (long)ART_MAX_PIXELS)
        return -1;
    px = stbi_load_from_memory(data, (int)size, &sw, &sh, &comp, 3);
    if (px == NULL)
        return -1;

    /* Box-average each destination pixel over its source block so large
     * covers downscale cleanly (decode happens once per track change). */
    for (dy = 0; dy < h; dy++) {
        int y0 = dy * sh / h;
        int y1 = (dy + 1) * sh / h;
        if (y1 <= y0)
            y1 = y0 + 1;
        for (dx = 0; dx < w; dx++) {
            int x0 = dx * sw / w;
            int x1 = (dx + 1) * sw / w;
            uint32_t r = 0, g = 0, b = 0, cnt = 0;
            int sx, sy;

            if (x1 <= x0)
                x1 = x0 + 1;
            for (sy = y0; sy < y1; sy++) {
                const uint8_t *row = px + ((size_t)sy * (size_t)sw + (size_t)x0) * 3u;
                for (sx = x0; sx < x1; sx++) {
                    r += row[0];
                    g += row[1];
                    b += row[2];
                    row += 3;
                    cnt++;
                }
            }
            r /= cnt;
            g /= cnt;
            b /= cnt;
            out[dy * w + dx] =
                (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }

    stbi_image_free(px);
    return 0;
}
