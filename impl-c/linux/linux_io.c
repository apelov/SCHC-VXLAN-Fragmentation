#define _GNU_SOURCE
#include "linux_io.h"
#include "daemon.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/filter.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BATCH 32
#define PACKET_SIZE 65536
#define RAW_RCVBUF_BYTES (16 * 1024 * 1024)
struct runtime {
    const struct schcvx_config *config;
    struct schcvx_daemon *daemon;
    int tap[SCHCVX_MAX_ASSOC], tx[SCHCVX_MAX_ASSOC];
    int raw, epoll, signals;
    uint64_t counter_due[SCHCVX_MAX_ASSOC];
    uint64_t refused_logged[SCHCVX_MAX_ASSOC];
    int refused_seen[SCHCVX_MAX_ASSOC];
};
static void log_error(const char *what) { fprintf(stderr, "ERROR: %s: %s\n", what, strerror(errno)); }
static uint64_t now_ns(void *unused)
{
    struct timespec t;
    (void)unused;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) { log_error("clock_gettime"); exit(1); }
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static uint64_t add_ns(uint64_t a, uint64_t b) { return UINT64_MAX - a < b ? UINT64_MAX : a + b; }
static void warn_refused(struct runtime *r, size_t i)
{
    struct sockaddr_in peer;
    socklen_t len = sizeof peer;
    char text[INET_ADDRSTRLEN] = "?";
    uint64_t now = now_ns(NULL);
    if (r->refused_seen[i] && now - r->refused_logged[i] < UINT64_C(1000000000)) { return; }
    r->refused_seen[i] = 1;
    r->refused_logged[i] = now;
    if (getpeername(r->tx[i], (struct sockaddr *)&peer, &len) == 0 && peer.sin_family == AF_INET) {
        inet_ntop(AF_INET, &peer.sin_addr, text, sizeof text);
        fprintf(stderr, "WARNING: UDP send: peer %s:%u refused a datagram (ICMP port unreachable); retrying\n", text, (unsigned)ntohs(peer.sin_port));
    } else {
        fprintf(stderr, "WARNING: UDP send: peer refused a datagram (ICMP port unreachable); retrying\n");
    }
}
static int send_packet(void *ctx, size_t i, const uint8_t *p, size_t n)
{
    struct runtime *r = ctx;
    ssize_t sent = send(r->tx[i], p, n, 0);
    if (sent < 0 && errno == ECONNREFUSED) {
        /* A pending ICMP error from an earlier datagram is reported by this call and consumed; retry once. */
        warn_refused(r, i);
        sent = send(r->tx[i], p, n, 0);
    }
    if (sent < 0 || (size_t)sent != n) { if (sent >= 0) errno = EIO; log_error("UDP send"); return -1; }
    return 0;
}
static int write_frame(void *ctx, size_t i, const uint8_t *p, size_t n)
{
    struct runtime *r = ctx;
    ssize_t written = write(r->tap[i], p, n);
    if (written < 0 || (size_t)written != n) { if (written >= 0) errno = EIO; log_error("TAP write"); return -1; }
    return 0;
}
static int attach_filter(int fd, struct sock_filter *code, size_t count)
{
    struct sock_fprog filter;
    filter.len = (unsigned short)count;
    filter.filter = code;
    return setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof filter);
}
static int open_tap(const struct schcvx_config_assoc *a)
{
    struct ifreq req;
    int fd, ctl;
    if (strlen(a->tap) >= IFNAMSIZ) { errno = ENAMETOOLONG; return -1; }
    fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    memset(&req, 0, sizeof req);
    memcpy(req.ifr_name, a->tap, strlen(a->tap) + 1);
    req.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (ioctl(fd, TUNSETIFF, &req) < 0) { close(fd); return -1; }
    ctl = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (ctl < 0) { close(fd); return -1; }
    req.ifr_mtu = (int)(a->tx.max_frame_len - 14U);
    if (ioctl(ctl, SIOCSIFMTU, &req) < 0) log_error("setting TAP MTU (continuing)");
    if (ioctl(ctl, SIOCGIFFLAGS, &req) < 0) { close(ctl); close(fd); return -1; }
    req.ifr_flags |= IFF_UP;
    if (ioctl(ctl, SIOCSIFFLAGS, &req) < 0) { close(ctl); close(fd); return -1; }
    close(ctl);
    return fd;
}
static int open_tx(const struct schcvx_config_assoc *a)
{
    struct sockaddr_in local = {0}, peer = {0};
    struct sock_filter drop[] = { BPF_STMT(BPF_RET | BPF_K, 0) };
    int fd, probe = IP_PMTUDISC_PROBE;
    local.sin_family = AF_INET; local.sin_port = htons(a->src_port);
    peer.sin_family = AF_INET; peer.sin_port = htons(a->udp_port);
    if (inet_pton(AF_INET, a->local_ip, &local.sin_addr) != 1 || inet_pton(AF_INET, a->peer_ip, &peer.sin_addr) != 1) { errno = EINVAL; return -1; }
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (setsockopt(fd, IPPROTO_IP, IP_MTU_DISCOVER, &probe, sizeof probe) < 0 ||
        attach_filter(fd, drop, 1) < 0 || bind(fd, (struct sockaddr *)&local, sizeof local) < 0 ||
        connect(fd, (struct sockaddr *)&peer, sizeof peer) < 0) { int e = errno; close(fd); errno = e; return -1; }
    return fd;
}
static void set_raw_rcvbuf(int fd)
{
    int want = RAW_RCVBUF_BYTES, got = 0;
    socklen_t len = sizeof got;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &want, sizeof want) < 0 &&
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &want, sizeof want) < 0) {
        fprintf(stderr, "WARNING: raw socket receive buffer request failed: %s (continuing)\n", strerror(errno));
    }
    if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &got, &len) < 0) {
        fprintf(stderr, "WARNING: reading raw socket receive buffer: %s\n", strerror(errno));
        return;
    }
    /* The kernel reports twice the requested value (bookkeeping overhead). */
    if (got < want) fprintf(stderr, "WARNING: raw socket receive buffer is %d bytes, requested %d\n", got, want);
    else fprintf(stderr, "INFO: raw socket receive buffer is %d bytes\n", got);
}
static int open_raw(const struct schcvx_config *c)
{
    struct sock_filter code[2 * SCHCVX_MAX_ASSOC + 3];
    size_t i, n = 0;
    int fd = socket(AF_INET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
    if (fd < 0) return -1;
    set_raw_rcvbuf(fd);
    code[n++] = (struct sock_filter)BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, 0);
    code[n++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_H | BPF_IND, 2);
    for (i = 0; i < c->count; i++) {
        code[n++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, c->assoc[i].udp_port, 0, 1);
        code[n++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, UINT32_MAX);
    }
    code[n++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, 0);
    if (attach_filter(fd, code, n) < 0) { int e = errno; close(fd); errno = e; return -1; }
    return fd;
}
static uint16_t be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t sum_bytes(uint32_t sum, const uint8_t *p, size_t n)
{
    while (n >= 2) { sum += be16(p); p += 2; n -= 2; }
    if (n) sum += (uint32_t)p[0] << 8;
    return sum;
}
static void process_packet(struct runtime *r, const uint8_t *p, size_t n, uint64_t timestamp)
{
    size_t total, udp_len;
    uint32_t sum;
    char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
    if (n < 28 || p[0] != 0x45 || p[9] != IPPROTO_UDP || (be16(p + 6) & 0x3fffU)) return;
    total = be16(p + 2); udp_len = be16(p + 24);
    if (total < 28 || total > n || udp_len < 8 || udp_len != total - 20) return;
    sum = sum_bytes(0, p + 12, 8) + IPPROTO_UDP + (uint32_t)udp_len;
    sum = sum_bytes(sum, p + 20, udp_len);
    while (sum >> 16) sum = (sum & 0xffffU) + (sum >> 16);
    if (!inet_ntop(AF_INET, p + 12, src, sizeof src) || !inet_ntop(AF_INET, p + 16, dst, sizeof dst)) return;
    if (be16(p + 26) == 0 || sum != 0xffffU) { schcvx_count_bad_checksum(r->daemon, dst, be16(p + 22), src); return; }
    (void)schcvx_process_datagram(r->daemon, dst, be16(p + 22), src, p + 28, udp_len - 8, timestamp);
}
static int receive_batch(struct runtime *r)
{
    struct mmsghdr msgs[BATCH]; struct iovec iov[BATCH];
    uint8_t *buffers = malloc((size_t)BATCH * PACKET_SIZE);
    int count, i; uint64_t timestamp;
    if (!buffers) { log_error("receive buffers"); return -1; }
    memset(msgs, 0, sizeof msgs);
    for (i = 0; i < BATCH; i++) { iov[i].iov_base = buffers + (size_t)i * PACKET_SIZE; iov[i].iov_len = PACKET_SIZE; msgs[i].msg_hdr.msg_iov = &iov[i]; msgs[i].msg_hdr.msg_iovlen = 1; }
    count = recvmmsg(r->raw, msgs, BATCH, MSG_DONTWAIT, NULL);
    if (count < 0) { int e = errno; free(buffers); if (e == EAGAIN || e == EINTR) return 0; errno = e; log_error("recvmmsg"); return -1; }
    timestamp = now_ns(NULL);
    for (i = 0; i < count; i++) if (!(msgs[i].msg_hdr.msg_flags & MSG_TRUNC)) process_packet(r, iov[i].iov_base, msgs[i].msg_len, timestamp);
    free(buffers); return 0;
}
static int snapshot(struct runtime *r, size_t index)
{
    const char *path = r->config->assoc[index].counters_file;
    size_t i, len = strlen(path) + 16; char *tmp = malloc(len); FILE *f; int fd, failed = 0;
    if (!tmp) return -1;
    snprintf(tmp, len, "%s.tmp.XXXXXX", path);
    fd = mkstemp(tmp);
    if (fd < 0) { log_error("creating counters file"); free(tmp); return -1; }
    f = fdopen(fd, "w");
    if (!f) { log_error("opening counters stream"); close(fd); unlink(tmp); free(tmp); return -1; }
    for (i = 0; i < r->config->count; i++) if (!strcmp(path, r->config->assoc[i].counters_file)) {
        const struct schcvx_counters *c = schcvx_daemon_counters(r->daemon, i);
        size_t need = schcvx_counters_format(c, NULL, 0); char *text = malloc(need + 1);
        if (!text) { failed = 1; break; }
        schcvx_counters_format(c, text, need + 1);
        if (fprintf(f, "[association %s]\n%s", r->config->assoc[i].name, text) < 0) failed = 1;
        free(text);
        if (failed) break;
    }
    if (fclose(f) != 0) failed = 1;
    if (!failed && rename(tmp, path) != 0) failed = 1;
    if (failed) { log_error("writing counters"); unlink(tmp); }
    free(tmp); return failed ? -1 : 0;
}
static int add_fd(int ep, int fd, uint32_t id)
{
    struct epoll_event ev = {0}; ev.events = EPOLLIN; ev.data.u32 = id;
    return epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);
}
int schcvx_linux_run(const struct schcvx_config *config)
{
    struct runtime r; struct schcvx_io io; sigset_t mask, oldmask; struct epoll_event events[SCHCVX_MAX_ASSOC + 2];
    size_t i; int result = 1, running = 1, masked = 0; uint64_t now;
    memset(&r, 0, sizeof r); r.config = config; r.raw = r.epoll = r.signals = -1;
    for (i = 0; i < SCHCVX_MAX_ASSOC; i++) r.tap[i] = r.tx[i] = -1;
    sigemptyset(&mask); sigaddset(&mask, SIGINT); sigaddset(&mask, SIGTERM); sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, &oldmask) < 0) { log_error("blocking signals"); goto done; } masked = 1;
    r.signals = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    r.epoll = epoll_create1(EPOLL_CLOEXEC);
    if (r.signals < 0 || r.epoll < 0) { log_error("creating event loop"); goto done; }
    r.raw = open_raw(config);
    if (r.raw < 0) { log_error("opening raw socket"); goto done; }
    if (add_fd(r.epoll, r.raw, SCHCVX_MAX_ASSOC) < 0 || add_fd(r.epoll, r.signals, SCHCVX_MAX_ASSOC + 1) < 0) { log_error("registering event source"); goto done; }
    for (i = 0; i < config->count; i++) {
        r.tap[i] = open_tap(&config->assoc[i]);
        if (r.tap[i] < 0) { log_error("opening TAP"); goto done; }
        r.tx[i] = open_tx(&config->assoc[i]);
        if (r.tx[i] < 0) { log_error("opening transmit socket"); goto done; }
        if (add_fd(r.epoll, r.tap[i], (uint32_t)i) < 0) { log_error("registering TAP"); goto done; }
    }
    now = now_ns(NULL); io = (struct schcvx_io){&r, send_packet, write_frame, now_ns};
    r.daemon = schcvx_daemon_create(config, &io, now);
    if (!r.daemon) { fprintf(stderr, "ERROR: creating portable daemon\n"); goto done; }
    for (i = 0; i < config->count; i++) r.counter_due[i] = add_ns(now, config->assoc[i].counters_interval);
    while (running) {
        uint64_t deadline, wait_ns; int timeout = -1, n, j;
        now = now_ns(NULL);
        if (!schcvx_next_deadline(r.daemon, &deadline)) deadline = UINT64_MAX;
        for (i = 0; i < config->count; i++) if (r.counter_due[i] < deadline) deadline = r.counter_due[i];
        if (deadline != UINT64_MAX) { wait_ns = deadline > now ? deadline - now : 0; wait_ns = wait_ns / 1000000U + (wait_ns % 1000000U != 0); timeout = wait_ns > INT_MAX ? INT_MAX : (int)wait_ns; }
        n = epoll_wait(r.epoll, events, SCHCVX_MAX_ASSOC + 2, timeout);
        if (n < 0) { if (errno == EINTR) continue; log_error("epoll_wait"); goto done; }
        for (j = 0; j < n; j++) {
            uint32_t id = events[j].data.u32;
            if (id == SCHCVX_MAX_ASSOC) { if (receive_batch(&r) < 0) goto done; }
            else if (id == SCHCVX_MAX_ASSOC + 1) {
                struct signalfd_siginfo si; ssize_t got;
                while ((got = read(r.signals, &si, sizeof si)) == (ssize_t)sizeof si) {
                    if (si.ssi_signo == SIGUSR1) { for (i = 0; i < config->count; i++) (void)snapshot(&r, i); }
                    else running = 0;
                }
            } else if (id < config->count) {
                uint8_t frame[65536]; ssize_t size;
                while ((size = read(r.tap[id], frame, sizeof frame)) > 0) (void)schcvx_process_frame(r.daemon, id, frame, (size_t)size, now_ns(NULL));
                if (size < 0 && errno != EAGAIN && errno != EINTR) { log_error("TAP read"); goto done; }
            }
        }
        now = now_ns(NULL); schcvx_sweep(r.daemon, now);
        for (i = 0; i < config->count; i++) if (now >= r.counter_due[i]) { (void)snapshot(&r, i); r.counter_due[i] = add_ns(now, config->assoc[i].counters_interval); }
    }
    result = 0;
done:
    schcvx_daemon_destroy(r.daemon);
    for (i = 0; i < config->count; i++) { if (r.tap[i] >= 0) close(r.tap[i]); if (r.tx[i] >= 0) close(r.tx[i]); }
    if (r.raw >= 0) close(r.raw);
    if (r.signals >= 0) close(r.signals);
    if (r.epoll >= 0) close(r.epoll);
    if (masked) sigprocmask(SIG_SETMASK, &oldmask, NULL);
    return result;
}
