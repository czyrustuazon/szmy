/* Simple authenticated FTP server for the SD card.
 * Host-testable path/auth helpers are always compiled; the socket/thread
 * implementation is 3DS-only so the coverage gate stays deterministic. */

#include "ftp_server.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define FTP_AUTH_NEED_USER 0
#define FTP_AUTH_NEED_PASS 1
#define FTP_AUTH_OK        2

/* Look-alike characters are omitted (I/l/1, O/o/0, L/i) so the password on
 * the top screen can be typed without guessing. */
static const char FTP_PASS_ALPHABET[] =
    "ABCDEFGHJKMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789";
#define FTP_PASS_ALPHABET_N 54

/* --- Pure helpers (host-tested) ------------------------------------------ */

void ftp_password_from_bytes(const unsigned char *bytes, size_t n,
                             char out[FTP_PASS_LEN + 1])
{
    size_t i;

    for (i = 0; i < FTP_PASS_LEN; i++) {
        unsigned char b = (bytes != NULL && i < n) ? bytes[i] : 0;
        out[i] = FTP_PASS_ALPHABET[b % FTP_PASS_ALPHABET_N];
    }
    out[FTP_PASS_LEN] = '\0';
}

int ftp_auth_user(int *state, const char *user)
{
    if (state == NULL || user == NULL)
        return 530;
    if (strcmp(user, FTP_USER) != 0) {
        *state = FTP_AUTH_NEED_USER;
        return 530;
    }
    *state = FTP_AUTH_NEED_PASS;
    return 331;
}

int ftp_auth_pass(int *state, const char *expected, const char *pass)
{
    if (state == NULL || expected == NULL || pass == NULL)
        return 530;
    if (*state != FTP_AUTH_NEED_PASS) {
        *state = FTP_AUTH_NEED_USER;
        return 503;
    }
    if (strcmp(pass, expected) != 0) {
        *state = FTP_AUTH_NEED_USER;
        return 530;
    }
    *state = FTP_AUTH_OK;
    return 230;
}

/* FAT32 long filenames allow up to 255 characters per component. */
#define FTP_NAME_MAX 255

/* Collapse ".", reject ".." escapes, produce a cleaned absolute virtual path. */
int ftp_path_normalize(const char *cwd_virt, const char *arg,
                       char *out_virt, size_t out_n)
{
    char   raw[512];
    struct {
        const char *s;
        size_t      len;
    }      parts[32];
    int    nparts = 0;
    char  *p;
    size_t i, used;

    if (out_virt == NULL || out_n < 2)
        return -1;
    if (cwd_virt == NULL || cwd_virt[0] != '/')
        cwd_virt = "/";

    {
        int n;
        if (arg == NULL || arg[0] == '\0')
            n = snprintf(raw, sizeof(raw), "%s", cwd_virt);
        else if (arg[0] == '/')
            n = snprintf(raw, sizeof(raw), "%s", arg);
        else if (strcmp(cwd_virt, "/") == 0)
            n = snprintf(raw, sizeof(raw), "/%s", arg);
        else
            n = snprintf(raw, sizeof(raw), "%s/%s", cwd_virt, arg);
        if (n < 0 || n >= (int)sizeof(raw))
            return -1;
    }

    for (i = 0; raw[i] != '\0'; i++) {
        if (raw[i] == '\\')
            raw[i] = '/';
    }

    p = raw;
    while (*p != '\0') {
        char *start;
        size_t len;

        while (*p == '/')
            p++;
        if (*p == '\0')
            break;
        start = p;
        while (*p != '\0' && *p != '/')
            p++;
        len = (size_t)(p - start);
        if (len == 1 && start[0] == '.')
            continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (nparts > 0)
                nparts--;
            continue;
        }
        if (nparts >= 32 || len > FTP_NAME_MAX)
            return -1;
        parts[nparts].s   = start;
        parts[nparts].len = len;
        nparts++;
    }

    if (nparts == 0) {
        strncpy(out_virt, "/", out_n - 1);
        out_virt[out_n - 1] = '\0';
        return 0;
    }

    used = 0;
    for (i = 0; i < (size_t)nparts; i++) {
        if (used + 1 + parts[i].len + 1 > out_n)
            return -1;
        out_virt[used++] = '/';
        memcpy(out_virt + used, parts[i].s, parts[i].len);
        used += parts[i].len;
    }
    out_virt[used] = '\0';
    return 0;
}

int ftp_path_to_fs(const char *virt, char *out_fs, size_t out_n)
{
    char cleaned[512];
    int  n;

    if (out_fs == NULL || out_n < 8)
        return -1;
    if (ftp_path_normalize("/", virt ? virt : "/", cleaned, sizeof(cleaned)) != 0)
        return -1;

    if (strcmp(cleaned, "/") == 0)
        n = snprintf(out_fs, out_n, "%s/", FTP_ROOT_FS);
    else /* cleaned is "/foo/bar" → "sdmc:/foo/bar" */
        n = snprintf(out_fs, out_n, "%s%s", FTP_ROOT_FS, cleaned);
    return (n < 0 || n >= (int)out_n) ? -1 : 0;
}

#ifndef UNIT_TEST

#include "audio.h"
#include "jptext.h"
#include "topbg.h"

#include <3ds.h>
#include <citro2d.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define SOC_ALIGN       0x1000
#define SOC_BUFFERSIZE  0x100000
#define FTP_STACK       0x10000
#define FTP_LINE_MAX    1024
#define FTP_XFER_CHUNK  8192
#define DATA_PORT_BASE  5001

static u32       *s_soc_buf;
static int        s_soc_ok;
static int        s_ndmu_locked;
static int        s_active;
static int        s_stop;
static Thread     s_thread;
static LightLock  s_lock;

static char s_ip[32];
static char s_pass[FTP_PASS_LEN + 1];
static char s_msg[64];
static int  s_listen_fd = -1;

static void status_set(const char *msg)
{
    LightLock_Lock(&s_lock);
    if (msg != NULL) {
        strncpy(s_msg, msg, sizeof(s_msg) - 1);
        s_msg[sizeof(s_msg) - 1] = '\0';
    }
    LightLock_Unlock(&s_lock);
}

static void close_fd(int *fd)
{
    if (fd != NULL && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static int send_all(int fd, const void *buf, size_t n)
{
    const char *p = (const char *)buf;
    size_t      left = n;

    while (left > 0) {
        ssize_t w = send(fd, p, left, 0);
        if (w <= 0)
            return -1;
        p += w;
        left -= (size_t)w;
    }
    return 0;
}

static int reply(int fd, int code, const char *text)
{
    char line[FTP_LINE_MAX];
    int  n = snprintf(line, sizeof(line), "%d %s\r\n", code, text ? text : "");
    if (n <= 0 || n >= (int)sizeof(line))
        return -1;
    return send_all(fd, line, (size_t)n);
}

static int make_listen(uint16_t port)
{
    int                fd;
    int                yes = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0
        || listen(fd, 1) != 0) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

static int accept_ready(int listen_fd, int timeout_ms)
{
    struct pollfd pfd;
    int           cfd;

    if (listen_fd < 0)
        return -1;
    pfd.fd     = listen_fd;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, timeout_ms) <= 0)
        return -1;
    cfd = accept(listen_fd, NULL, NULL);
    if (cfd >= 0)
        fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL, 0) & ~O_NONBLOCK);
    return cfd;
}

static int read_line(int fd, char *buf, size_t n)
{
    size_t i = 0;

    while (i + 1 < n) {
        char    c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0)
            return -1;
        if (c == '\n')
            break;
        if (c == '\r')
            continue;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return (int)i;
}

static void split_cmd(char *line, char **cmd, char **arg)
{
    char *p = line;

    while (*p && isspace((unsigned char)*p))
        p++;
    *cmd = p;
    while (*p && !isspace((unsigned char)*p)) {
        *p = (char)toupper((unsigned char)*p);
        p++;
    }
    if (*p) {
        *p++ = '\0';
        while (*p && isspace((unsigned char)*p))
            p++;
        *arg = p;
    } else {
        *arg = p;
    }
}

static int open_pasv(int *listen_out, char *msg, size_t msg_n)
{
    struct in_addr ip;
    struct in_addr nm, bc;
    uint16_t       port = DATA_PORT_BASE;
    int            fd   = -1;
    unsigned int   a, b, c, d, p1, p2;
    int            tries;

    if (SOCU_GetIPInfo(&ip, &nm, &bc) != 0)
        return -1;

    for (tries = 0; tries < 16; tries++) {
        fd = make_listen(port);
        if (fd >= 0)
            break;
        port++;
    }
    if (fd < 0)
        return -1;

    {
        uint32_t v = ntohl(ip.s_addr);
        a  = (v >> 24) & 0xff;
        b  = (v >> 16) & 0xff;
        c  = (v >> 8) & 0xff;
        d  = v & 0xff;
    }
    p1 = port / 256;
    p2 = port % 256;
    snprintf(msg, msg_n, "Entering Passive Mode (%u,%u,%u,%u,%u,%u)",
             a, b, c, d, p1, p2);
    *listen_out = fd;
    return 0;
}

static int accept_data(int pasv_fd)
{
    return accept_ready(pasv_fd, 10000);
}

static void list_dir(int data_fd, const char *fs_path, int names_only)
{
    DIR           *dir;
    struct dirent *ent;
    char           line[560];

    dir = opendir(fs_path);
    if (dir == NULL)
        return;

    while ((ent = readdir(dir)) != NULL) {
        struct stat st;
        char        full[832];
        int         is_dir;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (fs_path[0] != '\0' && fs_path[strlen(fs_path) - 1] == '/')
            snprintf(full, sizeof(full), "%s%s", fs_path, ent->d_name);
        else
            snprintf(full, sizeof(full), "%s/%s", fs_path, ent->d_name);
        if (stat(full, &st) != 0)
            continue;
        is_dir = S_ISDIR(st.st_mode);
        if (names_only) {
            snprintf(line, sizeof(line), "%s\r\n", ent->d_name);
        } else {
            snprintf(line, sizeof(line),
                     "%c%s 1 owner group %10lld Jan  1 00:00 %s\r\n",
                     is_dir ? 'd' : '-',
                     is_dir ? "rwxr-xr-x" : "rw-r--r--",
                     (long long)st.st_size, ent->d_name);
        }
        send_all(data_fd, line, strlen(line));
    }
    closedir(dir);
}

static int send_file(int data_fd, const char *fs_path)
{
    FILE  *f;
    char   buf[FTP_XFER_CHUNK];
    size_t n;

    f = fopen(fs_path, "rb");
    if (f == NULL)
        return -1;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (send_all(data_fd, buf, n) != 0) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

static int recv_file(int data_fd, const char *fs_path)
{
    FILE  *f;
    char   buf[FTP_XFER_CHUNK];
    ssize_t n;

    f = fopen(fs_path, "wb");
    if (f == NULL)
        return -1;
    while ((n = recv(data_fd, buf, sizeof(buf), 0)) > 0) {
        if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return (n < 0) ? -1 : 0;
}

typedef struct {
    int  ctrl;
    int  auth;
    int  pasv_fd;
    char cwd[512];
    char rnfr[560];
    char pass[FTP_PASS_LEN + 1];
} ftp_session_t;

static int ensure_data(ftp_session_t *s, int *data_fd)
{
    if (s->pasv_fd < 0) {
        reply(s->ctrl, 425, "Use PASV first");
        return -1;
    }
    *data_fd = accept_data(s->pasv_fd);
    close_fd(&s->pasv_fd);
    if (*data_fd < 0) {
        reply(s->ctrl, 425, "Can't open data connection");
        return -1;
    }
    return 0;
}

static void handle_session(ftp_session_t *s)
{
    char line[FTP_LINE_MAX];
    char *cmd, *arg;

    reply(s->ctrl, 220, "SZMY FTP ready");
    status_set("Client connected");

    while (!s_stop) {
        struct pollfd pfd;
        pfd.fd     = s->ctrl;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 200) <= 0)
            continue;
        if (read_line(s->ctrl, line, sizeof(line)) < 0)
            break;

        split_cmd(line, &cmd, &arg);

        if (strcmp(cmd, "USER") == 0) {
            int code = ftp_auth_user(&s->auth, arg);
            reply(s->ctrl, code,
                  code == 331 ? "Password required" : "Login incorrect");
        } else if (strcmp(cmd, "PASS") == 0) {
            int code = ftp_auth_pass(&s->auth, s->pass, arg);
            reply(s->ctrl, code,
                  code == 230 ? "Logged in" :
                  code == 503 ? "Login with USER first" : "Login incorrect");
            if (code == 230)
                status_set("Logged in");
        } else if (strcmp(cmd, "QUIT") == 0) {
            reply(s->ctrl, 221, "Goodbye");
            break;
        } else if (strcmp(cmd, "SYST") == 0) {
            reply(s->ctrl, 215, "UNIX Type: L8");
        } else if (strcmp(cmd, "FEAT") == 0) {
            const char *feat = "211-Features:\r\n SIZE\r\n UTF8\r\n211 End\r\n";
            send_all(s->ctrl, feat, strlen(feat));
        } else if (strcmp(cmd, "NOOP") == 0) {
            reply(s->ctrl, 200, "OK");
        } else if (strcmp(cmd, "TYPE") == 0) {
            reply(s->ctrl, 200, "Type set");
        } else if (s->auth != FTP_AUTH_OK) {
            reply(s->ctrl, 530, "Please login");
        } else if (strcmp(cmd, "PWD") == 0 || strcmp(cmd, "XPWD") == 0) {
            char msg[520];
            snprintf(msg, sizeof(msg), "\"%s\"", s->cwd);
            reply(s->ctrl, 257, msg);
        } else if (strcmp(cmd, "CWD") == 0 || strcmp(cmd, "XCWD") == 0) {
            char virt[512], fs[560];
            struct stat st;
            if (ftp_path_normalize(s->cwd, arg, virt, sizeof(virt)) != 0
                || ftp_path_to_fs(virt, fs, sizeof(fs)) != 0
                || stat(fs, &st) != 0 || !S_ISDIR(st.st_mode)) {
                reply(s->ctrl, 550, "Failed to change directory");
            } else {
                strncpy(s->cwd, virt, sizeof(s->cwd) - 1);
                s->cwd[sizeof(s->cwd) - 1] = '\0';
                reply(s->ctrl, 250, "Directory changed");
            }
        } else if (strcmp(cmd, "CDUP") == 0 || strcmp(cmd, "XCUP") == 0) {
            char virt[512];
            ftp_path_normalize(s->cwd, "..", virt, sizeof(virt));
            strncpy(s->cwd, virt, sizeof(s->cwd) - 1);
            s->cwd[sizeof(s->cwd) - 1] = '\0';
            reply(s->ctrl, 250, "Directory changed");
        } else if (strcmp(cmd, "PASV") == 0) {
            char msg[80];
            close_fd(&s->pasv_fd);
            if (open_pasv(&s->pasv_fd, msg, sizeof(msg)) != 0)
                reply(s->ctrl, 425, "Can't open passive port");
            else
                reply(s->ctrl, 227, msg);
        } else if (strcmp(cmd, "LIST") == 0 || strcmp(cmd, "NLST") == 0) {
            int  data_fd;
            char virt[512], fs[560];
            const char *targ = (arg && arg[0]) ? arg : ".";
            if (ftp_path_normalize(s->cwd, targ, virt, sizeof(virt)) != 0
                || ftp_path_to_fs(virt, fs, sizeof(fs)) != 0) {
                reply(s->ctrl, 550, "Failed");
            } else if (ensure_data(s, &data_fd) == 0) {
                reply(s->ctrl, 150, "Opening data connection");
                list_dir(data_fd, fs, strcmp(cmd, "NLST") == 0);
                close(data_fd);
                reply(s->ctrl, 226, "Transfer complete");
            }
        } else if (strcmp(cmd, "RETR") == 0) {
            int  data_fd;
            char virt[512], fs[560];
            struct stat st;
            if (!arg || !arg[0]
                || ftp_path_normalize(s->cwd, arg, virt, sizeof(virt)) != 0
                || ftp_path_to_fs(virt, fs, sizeof(fs)) != 0
                || stat(fs, &st) != 0 || !S_ISREG(st.st_mode)) {
                reply(s->ctrl, 550, "File unavailable");
            } else if (ensure_data(s, &data_fd) == 0) {
                reply(s->ctrl, 150, "Opening data connection");
                if (send_file(data_fd, fs) != 0)
                    reply(s->ctrl, 426, "Transfer aborted");
                else
                    reply(s->ctrl, 226, "Transfer complete");
                close(data_fd);
            }
        } else if (strcmp(cmd, "STOR") == 0) {
            int  data_fd;
            char virt[512], fs[560];
            if (!arg || !arg[0]
                || ftp_path_normalize(s->cwd, arg, virt, sizeof(virt)) != 0
                || ftp_path_to_fs(virt, fs, sizeof(fs)) != 0) {
                reply(s->ctrl, 550, "Failed");
            } else if (ensure_data(s, &data_fd) == 0) {
                reply(s->ctrl, 150, "Opening data connection");
                if (recv_file(data_fd, fs) != 0)
                    reply(s->ctrl, 426, "Transfer aborted");
                else
                    reply(s->ctrl, 226, "Transfer complete");
                close(data_fd);
            }
        } else if (strcmp(cmd, "DELE") == 0) {
            char virt[512], fs[560];
            if (!arg || !arg[0]
                || ftp_path_normalize(s->cwd, arg, virt, sizeof(virt)) != 0
                || ftp_path_to_fs(virt, fs, sizeof(fs)) != 0
                || unlink(fs) != 0)
                reply(s->ctrl, 550, "Delete failed");
            else
                reply(s->ctrl, 250, "Deleted");
        } else if (strcmp(cmd, "MKD") == 0 || strcmp(cmd, "XMKD") == 0) {
            char virt[512], fs[560];
            if (!arg || !arg[0]
                || ftp_path_normalize(s->cwd, arg, virt, sizeof(virt)) != 0
                || ftp_path_to_fs(virt, fs, sizeof(fs)) != 0
                || mkdir(fs, 0777) != 0)
                reply(s->ctrl, 550, "Create failed");
            else {
                char msg[530];
                snprintf(msg, sizeof(msg), "\"%s\" created", virt);
                reply(s->ctrl, 257, msg);
            }
        } else if (strcmp(cmd, "RMD") == 0 || strcmp(cmd, "XRMD") == 0) {
            char virt[512], fs[560];
            if (!arg || !arg[0]
                || ftp_path_normalize(s->cwd, arg, virt, sizeof(virt)) != 0
                || ftp_path_to_fs(virt, fs, sizeof(fs)) != 0
                || rmdir(fs) != 0)
                reply(s->ctrl, 550, "Remove failed");
            else
                reply(s->ctrl, 250, "Removed");
        } else if (strcmp(cmd, "RNFR") == 0) {
            char virt[512], fs[560];
            struct stat st;
            if (!arg || !arg[0]
                || ftp_path_normalize(s->cwd, arg, virt, sizeof(virt)) != 0
                || ftp_path_to_fs(virt, fs, sizeof(fs)) != 0
                || stat(fs, &st) != 0) {
                s->rnfr[0] = '\0';
                reply(s->ctrl, 550, "File unavailable");
            } else {
                strncpy(s->rnfr, fs, sizeof(s->rnfr) - 1);
                s->rnfr[sizeof(s->rnfr) - 1] = '\0';
                reply(s->ctrl, 350, "Ready for RNTO");
            }
        } else if (strcmp(cmd, "RNTO") == 0) {
            char virt[512], fs[560];
            if (s->rnfr[0] == '\0' || !arg || !arg[0]
                || ftp_path_normalize(s->cwd, arg, virt, sizeof(virt)) != 0
                || ftp_path_to_fs(virt, fs, sizeof(fs)) != 0
                || rename(s->rnfr, fs) != 0)
                reply(s->ctrl, 550, "Rename failed");
            else
                reply(s->ctrl, 250, "Renamed");
            s->rnfr[0] = '\0';
        } else if (strcmp(cmd, "SIZE") == 0) {
            char virt[512], fs[560];
            struct stat st;
            if (!arg || !arg[0]
                || ftp_path_normalize(s->cwd, arg, virt, sizeof(virt)) != 0
                || ftp_path_to_fs(virt, fs, sizeof(fs)) != 0
                || stat(fs, &st) != 0 || !S_ISREG(st.st_mode)) {
                reply(s->ctrl, 550, "File unavailable");
            } else {
                char msg[32];
                snprintf(msg, sizeof(msg), "%lld", (long long)st.st_size);
                reply(s->ctrl, 213, msg);
            }
        } else {
            reply(s->ctrl, 502, "Command not implemented");
        }
    }

    close_fd(&s->pasv_fd);
    status_set("Waiting for client");
}

static void ftp_worker(void *arg)
{
    (void)arg;
    status_set("Waiting for client");

    while (!s_stop) {
        int            cfd;
        ftp_session_t  sess;

        cfd = accept_ready(s_listen_fd, 200);
        if (cfd < 0)
            continue;

        memset(&sess, 0, sizeof(sess));
        sess.ctrl    = cfd;
        sess.auth    = FTP_AUTH_NEED_USER;
        sess.pasv_fd = -1;
        strncpy(sess.cwd, "/", sizeof(sess.cwd) - 1);
        LightLock_Lock(&s_lock);
        memcpy(sess.pass, s_pass, sizeof(sess.pass));
        LightLock_Unlock(&s_lock);

        handle_session(&sess);
        close(cfd);
    }
}

static int generate_password(char out[FTP_PASS_LEN + 1])
{
    unsigned char bytes[FTP_PASS_LEN];
    Result        rc;

    if (R_FAILED(psInit()))
        return -1;
    rc = PS_GenerateRandomBytes(bytes, sizeof(bytes));
    psExit();
    if (R_FAILED(rc))
        return -1;
    ftp_password_from_bytes(bytes, sizeof(bytes), out);
    return 0;
}

static int refresh_ip(void)
{
    struct in_addr ip, nm, bc;

    if (SOCU_GetIPInfo(&ip, &nm, &bc) != 0) {
        strncpy(s_ip, "0.0.0.0", sizeof(s_ip) - 1);
        s_ip[sizeof(s_ip) - 1] = '\0';
        return -1;
    }
    strncpy(s_ip, inet_ntoa(ip), sizeof(s_ip) - 1);
    s_ip[sizeof(s_ip) - 1] = '\0';
    return 0;
}

static void ndmu_lock(void)
{
    if (R_SUCCEEDED(ndmuInit())
        && R_SUCCEEDED(NDMU_EnterExclusiveState(NDM_EXCLUSIVE_STATE_INFRASTRUCTURE))
        && R_SUCCEEDED(NDMU_LockState())) {
        s_ndmu_locked = 1;
    }
}

static void ndmu_unlock(void)
{
    if (!s_ndmu_locked)
        return;
    NDMU_UnlockState();
    NDMU_LeaveExclusiveState();
    ndmuExit();
    s_ndmu_locked = 0;
}

static void ensure_lock(void)
{
    static int ready;
    if (!ready) {
        LightLock_Init(&s_lock);
        s_listen_fd = -1;
        ready = 1;
    }
}

static int ftp_start(void)
{
    char pass[FTP_PASS_LEN + 1];

    ensure_lock();
    if (s_active)
        return 1;

    audio_stop_wait();

    if (generate_password(pass) != 0) {
        status_set("Password RNG failed");
        return -1;
    }

    s_soc_buf = memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (s_soc_buf == NULL) {
        status_set("SOC alloc failed");
        return -2;
    }
    if (R_FAILED(socInit(s_soc_buf, SOC_BUFFERSIZE))) {
        free(s_soc_buf);
        s_soc_buf = NULL;
        status_set("socInit failed (Wi-Fi?)");
        return -3;
    }
    s_soc_ok = 1;
    ndmu_lock();

    if (refresh_ip() != 0) {
        status_set("No IP (enable Wi-Fi)");
        /* still try to listen; client may connect later */
    }

    s_listen_fd = make_listen(FTP_PORT);
    if (s_listen_fd < 0) {
        ndmu_unlock();
        socExit();
        s_soc_ok = 0;
        free(s_soc_buf);
        s_soc_buf = NULL;
        status_set("Bind port failed");
        return -4;
    }

    LightLock_Lock(&s_lock);
    memcpy(s_pass, pass, sizeof(s_pass));
    LightLock_Unlock(&s_lock);

    s_stop   = 0;
    s_thread = threadCreate(ftp_worker, NULL, FTP_STACK, 0x30, -1, false);
    if (s_thread == NULL) {
        close_fd(&s_listen_fd);
        ndmu_unlock();
        socExit();
        s_soc_ok = 0;
        free(s_soc_buf);
        s_soc_buf = NULL;
        status_set("Thread create failed");
        return -5;
    }

    s_active = 1;
    status_set("Waiting for client");
    return 1;
}

static void ftp_stop_internal(void)
{
    if (!s_active && s_thread == NULL && !s_soc_ok)
        return;

    s_stop = 1;
    close_fd(&s_listen_fd);
    if (s_thread != NULL) {
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }
    ndmu_unlock();
    if (s_soc_ok) {
        socExit();
        s_soc_ok = 0;
    }
    if (s_soc_buf != NULL) {
        free(s_soc_buf);
        s_soc_buf = NULL;
    }
    LightLock_Lock(&s_lock);
    s_pass[0] = '\0';
    s_ip[0]   = '\0';
    strncpy(s_msg, "Stopped", sizeof(s_msg) - 1);
    s_msg[sizeof(s_msg) - 1] = '\0';
    LightLock_Unlock(&s_lock);
    s_active = 0;
}

int ftp_toggle(void)
{
    ensure_lock();
    if (s_active) {
        ftp_stop_internal();
        return 0;
    }
    return ftp_start();
}

int ftp_is_active(void)
{
    return s_active;
}

void ftp_exit(void)
{
    ensure_lock();
    ftp_stop_internal();
}

void ftp_get_status(char *ip, size_t ip_n, char *pass, size_t pass_n,
                    char *msg, size_t msg_n)
{
    ensure_lock();
    LightLock_Lock(&s_lock);
    if (ip != NULL && ip_n > 0) {
        strncpy(ip, s_ip, ip_n - 1);
        ip[ip_n - 1] = '\0';
    }
    if (pass != NULL && pass_n > 0) {
        strncpy(pass, s_pass, pass_n - 1);
        pass[pass_n - 1] = '\0';
    }
    if (msg != NULL && msg_n > 0) {
        strncpy(msg, s_msg, msg_n - 1);
        msg[msg_n - 1] = '\0';
    }
    LightLock_Unlock(&s_lock);
}

void ftp_draw(void)
{
    char ip[32], pass[FTP_PASS_LEN + 1], msg[64], url[80];
    u32  white = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
    u32  grey  = C2D_Color32(0xE0, 0xE0, 0xE0, 0xFF);

    if (!jptext_ok())
        return;

    ftp_get_status(ip, sizeof(ip), pass, sizeof(pass), msg, sizeof(msg));
    if (ip[0] == '\0')
        strncpy(ip, "0.0.0.0", sizeof(ip) - 1);
    snprintf(url, sizeof(url), "ftp://%s:%d", ip, FTP_PORT);

    jptext_begin();
    topbg_draw_full();
    jptext_draw(JPTEXT_X, jptext_line_y(0), white, "FTP SERVER");
    jptext_draw(JPTEXT_X, jptext_line_y(2), grey, url);
    jptext_draw(JPTEXT_X, jptext_line_y(4), grey, "User: " FTP_USER);
    {
        char line[40];
        snprintf(line, sizeof(line), "Pass: %s", pass);
        jptext_draw(JPTEXT_X, jptext_line_y(5), white, line);
    }
    jptext_draw(JPTEXT_X, jptext_line_y(7), grey, "Root: sdmc:/");
    jptext_draw(JPTEXT_X, jptext_line_y(9), grey, msg);
    jptext_draw(JPTEXT_X, jptext_line_y(11), grey,
                "Tap folder icon again to stop");
    jptext_draw(JPTEXT_X, jptext_line_y(12), grey,
                "Music controls disabled while FTP is on");
    jptext_end();
}

#else /* UNIT_TEST — stub lifecycle so the object links in host runners */

static int s_active_stub;

int ftp_toggle(void)
{
    s_active_stub = !s_active_stub;
    return s_active_stub;
}

int ftp_is_active(void)
{
    return s_active_stub;
}

void ftp_exit(void)
{
    s_active_stub = 0;
}

void ftp_get_status(char *ip, size_t ip_n, char *pass, size_t pass_n,
                    char *msg, size_t msg_n)
{
    if (ip != NULL && ip_n > 0) {
        strncpy(ip, "127.0.0.1", ip_n - 1);
        ip[ip_n - 1] = '\0';
    }
    if (pass != NULL && pass_n > 0) {
        strncpy(pass, "Ab12Cd", pass_n - 1);
        pass[pass_n - 1] = '\0';
    }
    if (msg != NULL && msg_n > 0) {
        strncpy(msg, s_active_stub ? "Waiting for client" : "Stopped",
                msg_n - 1);
        msg[msg_n - 1] = '\0';
    }
}

#endif /* UNIT_TEST */
