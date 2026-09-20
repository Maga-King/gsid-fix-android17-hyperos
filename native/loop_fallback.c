/* SPDX-License-Identifier: Apache-2.0
 * Restore the single-file loop path in the device's own libfiemap/gsid.
 * The surrounding native MapImageDevice implementation keeps all its checks.
 * Linked into an added RX segment; uses the original executable's PLT entries.
 */
#include <fcntl.h>
#include <linux/loop.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/syscall.h>

static int format(char *out, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int rc = vsnprintf(out, size, fmt, args);
    va_end(args);
    return rc;
}
#define snprintf format

/* Android platform libc++ (__1), 64-bit default string ABI. No NDK C++ objects
 * cross this boundary. These borrowed views are never destroyed or resized. */
typedef union { uint64_t words[3]; unsigned char bytes[24]; } PlatformString;
extern bool stock_set_property(const PlatformString *, const PlatformString *);
extern void stock_delete(void *, size_t);
extern int stock_log(int, const char *, const char *, ...);

static const char *chars(const PlatformString *s) {
    return (s->bytes[0] & 1) ? (const char *)(uintptr_t)s->words[2] : (const char *)s->bytes + 1;
}
static size_t length(const PlatformString *s) {
    return (s->bytes[0] & 1) ? s->words[1] : s->bytes[0] >> 1;
}
static PlatformString view(const char *s) {
    PlatformString result = {0};
    size_t n = strlen(s);
    if (n <= 22) {
        result.bytes[0] = n << 1;
        memcpy(result.bytes + 1, s, n + 1);
    } else {
        result.words[0] = (n + 2) | 1;
        result.words[1] = n;
        result.words[2] = (uintptr_t)s;
    }
    return result;
}
static bool join(char *out, size_t capacity, const char *a, const char *b, const char *suffix) {
    int n = snprintf(out, capacity, "%s/%s%s", a, b, suffix);
    return n > 0 && (size_t)n < capacity;
}
static bool safe_name(const char *name) {
    if (!*name || strstr(name, "..")) return false;
    for (; *name; ++name) {
        if (!((*name >= 'a' && *name <= 'z') || (*name >= 'A' && *name <= 'Z') ||
              (*name >= '0' && *name <= '9') || *name == '_' || *name == '-' || *name == '.')) return false;
    }
    return true;
}

__attribute__((visibility("hidden")))
bool loop_fallback(void *manager, const PlatformString *image_name,
                   const int64_t *timeout_ms, PlatformString *out) {
    const PlatformString *metadata = (const PlatformString *)((char *)manager + 8);
    const PlatformString *data = (const PlatformString *)((char *)manager + 32);
    int header_fd = -1, file_fd = -1, control_fd = -1, loop_fd = -1, status_fd = -1;
    bool attached = false, created_status = false, ok = false;
    const char *stage = "validate paths";
    char header[4096], backing[4096], status_path[4096], list[4096], device[23], prop[256];
    char status[64];
    const char *name = chars(image_name), *data_dir = chars(data), *meta_dir = chars(metadata);
    if (length(data) >= 3500 || length(metadata) >= 3500 || length(image_name) > 128 ||
        !safe_name(name) || strncmp(data_dir, "/data/gsi/", 10) ||
        strncmp(meta_dir, "/metadata/gsi/", 14)) goto done;
    if (!join(header, sizeof(header), data_dir, name, ".img") ||
        !join(status_path, sizeof(status_path), meta_dir, name, ".status")) goto done;
    stage = "read split image list";
    header_fd = open(header, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (header_fd < 0) goto done;
    ssize_t n = read(header_fd, list, sizeof(list) - 1);
    if (n <= 0 || n == sizeof(list) - 1) goto done;
    list[n] = 0;
    close(header_fd); header_fd = -1;
    char *newline = strchr(list, '\n');
    if (newline) {
        *newline++ = 0;
        while (*newline == '\n') ++newline;
        if (*newline) { stage = "multi-part images are not supported by this build"; goto done; }
    }
    if (!safe_name(list) || !join(backing, sizeof(backing), data_dir, list, "")) goto done;
    stage = "open backing file";
    file_fd = open(backing, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (file_fd < 0) goto done;
    struct stat st;
    if (fstat(file_fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size % 4096) goto done;
    stage = "open loop-control";
    control_fd = open("/dev/loop-control", O_RDWR | O_CLOEXEC);
    if (control_fd < 0) goto done;
    /* Bound retries even if the caller supplied an unreasonable timeout. */
    int64_t budget = *timeout_ms;
    if (budget < 0) budget = 0;
    if (budget > 10000) budget = 10000;
    for (int64_t elapsed = 0; elapsed <= budget; elapsed += 10) {
        stage = "get free loop device";
        int number = ioctl(control_fd, LOOP_CTL_GET_FREE, 0);
        if (number < 0 || number > 99999) goto done;
        snprintf(device, sizeof(device), "/dev/block/loop%d", number);
        loop_fd = open(device, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
        if (loop_fd >= 0) {
            stage = "attach loop device";
            if (ioctl(loop_fd, LOOP_SET_FD, file_fd) == 0) { attached = true; break; }
            int saved = errno;
            close(loop_fd); loop_fd = -1;
            if (saved != EBUSY) goto done;
        } else if (errno != ENOENT) goto done;
        struct timespec delay = {0, 10000000};
        syscall(__NR_nanosleep, &delay, 0);
    }
    if (!attached) goto done;
    stage = "enable loop direct I/O";
    if (ioctl(loop_fd, LOOP_SET_BLOCK_SIZE, 4096) || ioctl(loop_fd, LOOP_SET_DIRECT_IO, 1)) goto done;
    stage = "write mapping status";
    int status_len = snprintf(status, sizeof(status), "loop:%s", device);
    status_fd = open(status_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0660);
    if (status_fd < 0) goto done;
    created_status = true;
    if (write(status_fd, status, status_len) != status_len || fsync(status_fd)) goto done;
    if (close(status_fd)) { status_fd = -1; goto done; }
    status_fd = -1;
    stage = "set mapped image property";
    snprintf(prop, sizeof(prop), "gsid.mapped_image.%s", name);
    PlatformString property = view(prop), value = view(device);
    if (!stock_set_property(&property, &value)) goto done;
    /* /dev/block/loopNNNNN fits in the platform's short-string storage. */
    if (out->bytes[0] & 1) stock_delete((void *)(uintptr_t)out->words[2], out->words[0] & ~1ULL);
    *out = value;
    ok = true;
    stock_log(4, "GSIDLoop17", "Mapped %s via %s (native Android 17 gsid)", name, device);
done:
    if (!ok) {
        stock_log(6, "GSIDLoop17", "Loop fallback failed at %s: errno=%d", stage, errno);
        if (attached && ioctl(loop_fd, LOOP_CLR_FD, 0))
            stock_log(6, "GSIDLoop17", "Failed to detach %s: errno=%d", device, errno);
        if (created_status) unlink(status_path);
    }
    if (status_fd >= 0) close(status_fd);
    if (loop_fd >= 0) close(loop_fd);
    if (control_fd >= 0) close(control_fd);
    if (file_fd >= 0) close(file_fd);
    if (header_fd >= 0) close(header_fd);
    return ok;
}
