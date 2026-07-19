#ifndef FTP_SERVER_H
#define FTP_SERVER_H

#include <stddef.h>

#define FTP_USER      "szmy"
#define FTP_PORT      5000
#define FTP_PASS_LEN  6
#define FTP_ROOT_FS   "sdmc:"
#define FTP_ROOT_VIRT "/"

/* Toggle FTP. Returns 1 if now active, 0 if stopped, negative on enable error. */
int ftp_toggle(void);

int ftp_is_active(void);

/* Stop the server if running (safe to call when idle). */
void ftp_exit(void);

/* Copy current connection details for UI. Buffers may be NULL. */
void ftp_get_status(char *ip, size_t ip_n, char *pass, size_t pass_n,
                    char *msg, size_t msg_n);

#ifndef UNIT_TEST
/* Draw the FTP connection screen on the top display (inside C3D frame). */
void ftp_draw(void);
#endif

#ifdef UNIT_TEST
/* Pure helpers exposed for host tests. */
int  ftp_path_normalize(const char *cwd_virt, const char *arg,
                        char *out_virt, size_t out_n);
int  ftp_path_to_fs(const char *virt, char *out_fs, size_t out_n);
void ftp_password_from_bytes(const unsigned char *bytes, size_t n,
                             char out[FTP_PASS_LEN + 1]);
int  ftp_auth_user(int *state, const char *user);
int  ftp_auth_pass(int *state, const char *expected, const char *pass);
#endif

#endif /* FTP_SERVER_H */
