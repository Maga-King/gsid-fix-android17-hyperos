// SPDX-License-Identifier: Apache-2.0
// Tests an isolated IImageService, never enables a DSU or calls openInstall.
#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

extern "C" AIBinder* AServiceManager_checkService(const char*);
static void* create(void* p) { return p; }
static void destroy(void*) {}
static binder_status_t incoming(AIBinder*, transaction_code_t, const AParcel*, AParcel*) { return STATUS_UNKNOWN_TRANSACTION; }
static bool alloc_string(void* context, int32_t length, char** buffer) {
    if (length < 0 || length > 4096) return false;
    *buffer = static_cast<char*>(malloc(length));
    *static_cast<char**>(context) = *buffer;
    return *buffer != nullptr;
}
static AParcel* prepare(AIBinder* binder) {
    AParcel* parcel = nullptr;
    if (AIBinder_prepareTransaction(binder, &parcel) != STATUS_OK) return nullptr;
    return parcel;
}
static AParcel* transact(AIBinder* binder, uint32_t code, AParcel* input, const char* label) {
    AParcel* output = nullptr;
    binder_status_t result = AIBinder_transact(binder, code, &input, &output, 0);
    AStatus* status = nullptr;
    if (result != STATUS_OK || AParcel_readStatusHeader(output, &status) != STATUS_OK) {
        fprintf(stderr, "%s: Binder transport error %d\n", label, result);
        if (output) AParcel_delete(output);
        return nullptr;
    }
    bool ok = AStatus_isOk(status);
    if (!ok) fprintf(stderr, "%s: exception=%d service=%d message=%s\n", label,
                     AStatus_getExceptionCode(status), AStatus_getServiceSpecificError(status), AStatus_getMessage(status));
    AStatus_delete(status);
    if (!ok) { AParcel_delete(output); return nullptr; }
    printf("OK %s\n", label); fflush(stdout);
    return output;
}
static bool simple(AIBinder* image, int code, const char* name, const char* label) {
    auto in = prepare(image); if (!in) return false;
    AParcel_writeString(in, name, strlen(name));
    auto out = transact(image, code, in, label);
    if (!out) return false;
    AParcel_delete(out); return true;
}
int main(int argc, char** argv) {
    const bool expect_failure = argc > 1 && !strcmp(argv[1], "--expect-map-failure");
    auto cls = AIBinder_Class_define("android.gsi.IGsiService", create, destroy, incoming);
    auto image_cls = AIBinder_Class_define("android.gsi.IImageService", create, destroy, incoming);
    auto gsi = AServiceManager_checkService("gsiservice");
    if (!gsi || !AIBinder_associateClass(gsi, cls)) { fprintf(stderr, "No gsid service\n"); return 1; }
    auto in = prepare(gsi);
    AParcel_writeString(in, "gsid17check", strlen("gsid17check"));
    auto out = transact(gsi, 23, in, "open isolated image service");
    if (!out) return 1;
    AIBinder* image = nullptr;
    auto rc = AParcel_readStrongBinder(out, &image); AParcel_delete(out);
    if (rc || !image || !AIBinder_associateClass(image, image_cls)) return 1;
    if (argc == 3 && !strcmp(argv[1], "--cleanup") && !strncmp(argv[2], "gsid17_probe_", 13)) {
        bool ok = simple(image, 4, argv[2], "cleanup owned mapping");
        if (ok) ok = simple(image, 2, argv[2], "cleanup owned image");
        AIBinder_decStrong(image); AIBinder_decStrong(gsi);
        return ok ? 0 : 1;
    }
    char name[64]; snprintf(name, sizeof(name), "gsid17_probe_%d", getpid());
    bool created = false, mapped = false, success = false;
    char* path = nullptr;
    in = prepare(image);
    AParcel_writeString(in, name, strlen(name));
    AParcel_writeInt64(in, 64LL * 1024 * 1024);
    AParcel_writeInt32(in, 0);
    AParcel_writeStrongBinder(in, nullptr);
    out = transact(image, 1, in, "create 64 MiB backing image");
    if (!out) goto cleanup;
    AParcel_delete(out); created = true;
    in = prepare(image);
    AParcel_writeString(in, name, strlen(name));
    AParcel_writeInt64(in, 8192);
    out = transact(image, 9, in, "native zeroFillNewImage on fresh image");
    if (!out) goto cleanup;
    AParcel_delete(out);
    in = prepare(image);
    AParcel_writeString(in, name, strlen(name));
    AParcel_writeInt32(in, 10000);
    out = transact(image, 3, in, "map backing image");
    if (!out) { success = expect_failure; goto cleanup; }
    mapped = true;
    {
        int32_t present = 0, size = 0;
        if (AParcel_readInt32(out, &present) || present != 1 || AParcel_readInt32(out, &size) ||
            size < 4 || AParcel_readString(out, &path, alloc_string)) {
            AParcel_delete(out); goto cleanup;
        }
    }
    AParcel_delete(out);
    printf("Mapped path: %s\n", path); fflush(stdout);
    if (expect_failure || strncmp(path, "/dev/block/loop", 15)) goto cleanup;
    {
        unsigned char written[4096], readback[4096];
        for (unsigned i = 0; i < sizeof(written); ++i) written[i] = (i * 37 + 17) & 255;
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) { perror("open mapped image"); goto cleanup; }
        bool initial_zero = pread(fd, readback, sizeof(readback), 4096) == sizeof(readback);
        for (unsigned i = 0; initial_zero && i < sizeof(readback); ++i) initial_zero = readback[i] == 0;
        if (!initial_zero) { close(fd); fprintf(stderr, "Fresh image was not zeroed\n"); goto cleanup; }
        printf("OK fresh image is zeroed\n");
        bool io_ok = pwrite(fd, written, sizeof(written), 4096) == sizeof(written) && fsync(fd) == 0 &&
                     pread(fd, readback, sizeof(readback), 4096) == sizeof(readback) &&
                     memcmp(written, readback, sizeof(written)) == 0;
        close(fd);
        if (!io_ok) { perror("read/write verify"); goto cleanup; }
        printf("OK block write, fsync and readback\n");
    }
    if (!simple(image, 4, name, "unmap image")) goto cleanup;
    mapped = false;
    {
        char backing[256];
        snprintf(backing, sizeof(backing), "/data/gsi/gsid17check/%s.img.0000", name);
        int fd = open(backing, O_RDONLY | O_CLOEXEC);
        unsigned char data[4096];
        bool io_ok = fd >= 0 && pread(fd, data, sizeof(data), 4096) == sizeof(data);
        if (fd >= 0) close(fd);
        for (unsigned i = 0; io_ok && i < sizeof(data); ++i) io_ok = data[i] == ((i * 37 + 17) & 255);
        if (!io_ok) { fprintf(stderr, "Backing file persistence failed\n"); goto cleanup; }
        printf("OK backing file data after unmap\n");
    }
    in = prepare(image);
    AParcel_writeString(in, name, strlen(name));
    AParcel_writeInt32(in, 10000);
    out = transact(image, 3, in, "remap same backing image");
    if (!out) goto cleanup;
    mapped = true;
    free(path); path = nullptr;
    {
        int32_t present = 0, size = 0;
        if (AParcel_readInt32(out, &present) || present != 1 || AParcel_readInt32(out, &size) ||
            size < 4 || AParcel_readString(out, &path, alloc_string)) {
            AParcel_delete(out); goto cleanup;
        }
    }
    AParcel_delete(out);
    {
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        unsigned char data[4096];
        bool io_ok = fd >= 0 && pread(fd, data, sizeof(data), 4096) == sizeof(data);
        if (fd >= 0) close(fd);
        for (unsigned i = 0; io_ok && i < sizeof(data); ++i) io_ok = data[i] == ((i * 37 + 17) & 255);
        if (!io_ok) { fprintf(stderr, "Remap persistence failed\n"); goto cleanup; }
        printf("OK data persists after remap\n");
    }
    success = true;
cleanup:
    if (mapped && !simple(image, 4, name, "cleanup unmap")) success = false;
    if (created && !simple(image, 2, name, "delete test image")) success = false;
    free(path);
    AIBinder_decStrong(image); AIBinder_decStrong(gsi);
    printf("RESULT: %s%s\n", success ? "PASS" : "FAIL", expect_failure ? " (stock negative control)" : "");
    return success ? 0 : 1;
}
