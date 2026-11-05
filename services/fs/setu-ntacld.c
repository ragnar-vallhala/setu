/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-ntacld: writes the security.NTACL extended attribute for the Setu
 * personality (README.md, "NT security descriptors"; protocol in
 * setu-ntacl.h).
 *
 * Reading a "security.*" attribute needs no privilege, so the wineserver
 * reads security.NTACL itself; writing one needs CAP_SYS_ADMIN, which this
 * helper holds instead of the wineserver. It is kept as narrow as it can be:
 *
 *   - it writes or removes the one attribute security.NTACL and nothing else;
 *   - it takes an open file descriptor from the client (SCM_RIGHTS), never a
 *     path, so it opens no files and follows no names;
 *   - the file must be a regular file or directory owned by the client's
 *     uid (SO_PEERCRED): the owner, who can already chmod it, and nobody else;
 *   - the file must be on a Setu volume: a file system mounted under one of
 *     the --volumes prefixes (default /var/lib/setu/volumes/ and
 *     /run/setu/volumes/);
 *   - the value must be a well-formed format-1 attribute: a format byte and a
 *     self-relative security descriptor whose parts lie inside it.
 *
 * It runs from setu-ntacld.socket with only CAP_SYS_ADMIN (setu-ntacld.service
 * restricts the rest), serves clients from one poll() loop without blocking on
 * any of them, and exits after --idle-exit seconds without clients.
 *
 * Errors in replies: EINVAL (bad request or value, not a file or directory),
 * EPERM (the client does not own the file), EXDEV (not on a Setu volume), or
 * the errno of fsetxattr()/fremovexattr() (ENOTSUP: the file system has no
 * security attributes).
 *
 *   setu-ntacld [--socket PATH] [--volumes PREFIX]... [--idle-exit SECONDS]
 *
 * Without --socket it expects the listening socket from systemd (fd 3,
 * LISTEN_FDS=1).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/un.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "setu-ntacl.h"

#define MAX_CLIENTS 64
#define MAX_VOLUMES 8

static const char *volumes[MAX_VOLUMES];
static int nvolumes;

static void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("setu-ntacld: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* undo the octal escapes of /proc/self/mountinfo (\040 for a space, ...) */
static void unescape(char *s)
{
    char *d = s;
    while (*s) {
        if (s[0] == '\\' && s[1] >= '0' && s[1] <= '3' && s[2] >= '0' && s[2] <= '7' && s[3] >= '0' && s[3] <= '7') {
            *d++ = (char)((s[1] - '0') * 64 + (s[2] - '0') * 8 + (s[3] - '0'));
            s += 4;
        } else
            *d++ = *s++;
    }
    *d = 0;
}

/* whether dev is the device of a file system mounted under a volume prefix */
static int on_setu_volume(dev_t dev)
{
    FILE *f = fopen("/proc/self/mountinfo", "re");
    char line[4096];
    int found = 0;

    if (!f) return 0;
    while (!found && fgets(line, sizeof(line), f)) {
        unsigned int maj, min;
        char mnt[4096];
        int i;
        /* id parent major:minor root mount-point ... */
        if (sscanf(line, "%*u %*u %u:%u %*s %4095s", &maj, &min, mnt) != 3) continue;
        if (makedev(maj, min) != dev) continue;
        unescape(mnt);
        for (i = 0; i < nvolumes; i++)
            if (!strncmp(mnt, volumes[i], strlen(volumes[i]))) found = 1;
    }
    fclose(f);
    return found;
}

static uint32_t get32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* a SID at offset off inside the descriptor sd of len bytes */
static int valid_sid(const unsigned char *sd, size_t len, uint32_t off)
{
    if (off < 20 || off > len || len - off < 8) return 0;
    if (sd[off] != 1 || sd[off + 1] > 15) return 0;  /* revision, sub-authority count */
    return len - off >= 8 + 4 * (size_t)sd[off + 1];
}

/* an ACL at offset off: its header and AceCount ACEs inside its AclSize */
static int valid_acl(const unsigned char *sd, size_t len, uint32_t off)
{
    size_t size, pos;
    unsigned int count, i;

    if (off < 20 || off > len || len - off < 8) return 0;
    if (sd[off] != 2 && sd[off] != 4) return 0;  /* ACL_REVISION, ACL_REVISION_DS */
    size = sd[off + 2] | sd[off + 3] << 8;
    count = sd[off + 4] | sd[off + 5] << 8;
    if (size < 8 || size > len - off) return 0;
    for (i = 0, pos = 8; i < count; i++) {
        size_t ace;
        if (size - pos < 4) return 0;
        ace = sd[off + pos + 2] | sd[off + pos + 3] << 8;
        if (ace < 4 || ace > size - pos) return 0;
        pos += ace;
    }
    return 1;
}

/* a format-1 value: the format byte and a self-relative security descriptor */
static int valid_value(const unsigned char *v, size_t len)
{
    const unsigned char *sd = v + 1;
    uint16_t control;
    uint32_t owner, group, sacl, dacl;

    if (len < 1 + 20 || len > SETU_NTACL_MAX || v[0] != SETU_NTACL_FORMAT) return 0;
    len--;
    control = sd[2] | sd[3] << 8;
    if (sd[0] != 1 || !(control & 0x8000)) return 0;  /* revision 1, SE_SELF_RELATIVE */
    owner = get32(sd + 4);
    group = get32(sd + 8);
    sacl = get32(sd + 12);
    dacl = get32(sd + 16);
    if (owner && !valid_sid(sd, len, owner)) return 0;
    if (group && !valid_sid(sd, len, group)) return 0;
    if ((control & 0x0010) && sacl && !valid_acl(sd, len, sacl)) return 0;  /* SE_SACL_PRESENT */
    if ((control & 0x0004) && dacl && !valid_acl(sd, len, dacl)) return 0;  /* SE_DACL_PRESENT */
    return 1;
}

static int handle(uid_t uid, int fd, const struct setu_ntacl_request *req, const unsigned char *data)
{
    struct stat st;
    int flags;

    if (fstat(fd, &st) == -1) return errno;
    if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) return EINVAL;
    if ((flags = fcntl(fd, F_GETFL)) == -1) return errno;
    if (flags & O_PATH) return EBADF;
    if (st.st_uid != uid) return EPERM;
    if (!on_setu_volume(st.st_dev)) return EXDEV;

    switch (req->op) {
    case SETU_NTACL_SET:
        if (!valid_value(data, req->len)) return EINVAL;
        return fsetxattr(fd, SETU_NTACL_XATTR, data, req->len, 0) == -1 ? errno : 0;
    case SETU_NTACL_REMOVE:
        if (req->len) return EINVAL;
        if (fremovexattr(fd, SETU_NTACL_XATTR) == -1 && errno != ENODATA) return errno;
        return 0;
    }
    return EINVAL;
}

/* serve one message of a client; returns 0 to keep the connection */
static int serve(int conn, uid_t uid)
{
    static unsigned char buf[sizeof(struct setu_ntacl_request) + SETU_NTACL_MAX + 1];
    union { struct cmsghdr hdr; char buf[CMSG_SPACE(4 * sizeof(int))]; } control;
    struct iovec iov = { buf, sizeof(buf) };
    struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1,
                          .msg_control = control.buf, .msg_controllen = sizeof(control.buf) };
    struct setu_ntacl_request req;
    struct setu_ntacl_reply reply = { SETU_NTACL_MAGIC, EINVAL };
    struct cmsghdr *cmsg;
    int fd = -1, nfds = 0;
    ssize_t n;

    n = recvmsg(conn, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (n == -1) return errno == EAGAIN || errno == EINTR ? 0 : -1;
    if (n == 0) return -1;

    /* take every descriptor sent, so none is leaked; exactly one is valid */
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) continue;
        int count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int), i, f;
        for (i = 0; i < count; i++) {
            memcpy(&f, CMSG_DATA(cmsg) + i * sizeof(int), sizeof(int));
            if (nfds++ == 0) fd = f;
            else close(f);
        }
    }

    if (nfds == 1 && !(msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) && (size_t)n >= sizeof(req)) {
        memcpy(&req, buf, sizeof(req));
        if (req.magic == SETU_NTACL_MAGIC && req.version == SETU_NTACL_VERSION &&
            req.len == (size_t)n - sizeof(req))
            reply.error = handle(uid, fd, &req, buf + sizeof(req));
    }
    if (fd != -1) close(fd);
    if (reply.error && reply.error != EXDEV && reply.error != ENOTSUP)
        log_msg("uid %u: request refused: %s", (unsigned)uid, strerror(reply.error));
    return send(conn, &reply, sizeof(reply), MSG_DONTWAIT | MSG_NOSIGNAL) == sizeof(reply) ? 0 : -1;
}

static int listen_socket(const char *path)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    int fd;

    if (!path) {
        const char *pid = getenv("LISTEN_PID"), *n = getenv("LISTEN_FDS");
        if (!pid || !n || atol(pid) != (long)getpid() || atoi(n) != 1) {
            log_msg("no --socket and no socket from systemd (LISTEN_FDS=1)");
            exit(2);
        }
        fcntl(3, F_SETFD, FD_CLOEXEC);
        return 3;
    }
    if (strlen(path) >= sizeof(addr.sun_path)) { log_msg("socket path too long"); exit(2); }
    strcpy(addr.sun_path, path);
    unlink(path);
    if ((fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)) == -1 ||
        bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1 || listen(fd, 16) == -1) {
        log_msg("%s: %s", path, strerror(errno));
        exit(1);
    }
    return fd;
}

int main(int argc, char **argv)
{
    struct pollfd pfd[1 + MAX_CLIENTS];
    uid_t uids[1 + MAX_CLIENTS];
    const char *path = NULL;
    int idle_exit = 60, nclients = 0, i, lfd;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--socket") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "--volumes") && i + 1 < argc && nvolumes < MAX_VOLUMES) volumes[nvolumes++] = argv[++i];
        else if (!strcmp(argv[i], "--idle-exit") && i + 1 < argc) idle_exit = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: setu-ntacld [--socket PATH] [--volumes PREFIX]... [--idle-exit SECONDS]\n");
            return 2;
        }
    }
    if (!nvolumes) {
        volumes[nvolumes++] = "/var/lib/setu/volumes/";
        volumes[nvolumes++] = "/run/setu/volumes/";
    }
    signal(SIGPIPE, SIG_IGN);
    lfd = listen_socket(path);
    pfd[0].fd = lfd;
    pfd[0].events = POLLIN;

    for (;;) {
        int r = poll(pfd, 1 + nclients, nclients || idle_exit <= 0 ? -1 : idle_exit * 1000);
        if (r == -1 && errno == EINTR) continue;
        if (r == -1) { log_msg("poll: %s", strerror(errno)); return 1; }
        if (r == 0) return 0;  /* idle: systemd starts us again on the next connection */

        for (i = 1; i <= nclients; i++) {
            if (!pfd[i].revents) continue;
            if (!(pfd[i].revents & POLLIN) || serve(pfd[i].fd, uids[i])) {
                close(pfd[i].fd);
                pfd[i] = pfd[nclients];
                uids[i] = uids[nclients];
                nclients--;
                i--;
            }
        }
        if (pfd[0].revents & POLLIN) {
            struct ucred cred;
            socklen_t len = sizeof(cred);
            int c = accept4(lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (c == -1) continue;
            if (nclients == MAX_CLIENTS || getsockopt(c, SOL_SOCKET, SO_PEERCRED, &cred, &len) == -1) {
                close(c);
                continue;
            }
            nclients++;
            pfd[nclients].fd = c;
            pfd[nclients].events = POLLIN;
            pfd[nclients].revents = 0;
            uids[nclients] = cred.uid;
        }
    }
}
