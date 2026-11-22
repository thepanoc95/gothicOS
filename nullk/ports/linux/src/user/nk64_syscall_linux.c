/*
 * Linux backend for the unmodified libnk64 wrappers.
 *
 * ABI v3 numbers are decoded here and never issued as host syscalls. Kernel-y
 * primitives are forwarded to /dev/nkabi3; VFS, tasks, exec and memory stay on
 * the userspace side of the reverse-monolithic boundary, just like the AMD64
 * port where those policies live in ring 3 tasks.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "libnk64.h"
#include "nullk_linux/abi3_ioctl.h"
#include "nullk_linux/user_shim.h"

#define NK64_LINUX_MAX_FILES 256u
#define NK64_LINUX_MAX_EXEC  0x100000u
#define NK64_LINUX_PAGE_SIZE 4096u

typedef struct {
    int fd;
    int used;
} nk64_file_slot_t;

typedef struct {
    pthread_t tid;
    void (*entry)(void);
    uint64_t arg;
    int used;
    int running;
} nk64_task_slot_t;

static pthread_mutex_t g_nk_lock = PTHREAD_MUTEX_INITIALIZER;
static nk64_file_slot_t g_files[NK64_LINUX_MAX_FILES];
static nk64_task_slot_t g_tasks[NK64_MAX_TASKS];
static pthread_t g_main_thread;
static int g_ctl_fd = -1;
static int g_initialized;

static uint64_t nk64_linux_ticks(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return (uint64_t)ts.tv_sec * 100u + (uint64_t)ts.tv_nsec / 10000000u;
}

static void nk64_ensure_ctl(void)
{
    if (g_initialized == 0) {
        /* init_pid1 normally calls this first; stay correct if a test calls
         * a wrapper directly. */
        (void)nk64_linux_init();
    }
}

int nk64_linux_init(void)
{
    if (g_initialized != 0) {
        return g_ctl_fd;
    }
    g_main_thread = pthread_self();
    g_ctl_fd = open(NK64_ABI3_DEVICE_NAME, O_RDWR | O_CLOEXEC);
    if (g_ctl_fd >= 0) {
        (void)ioctl(g_ctl_fd, NK64_ABI3_IOC_ATTACH);
    }
    g_initialized = 1;
    return g_ctl_fd;
}

int nk64_linux_control_fd(void)
{
    nk64_ensure_ctl();
    return g_ctl_fd;
}

uint32_t nk64_linux_map_path(const char *path, char *out, size_t cap)
{
    const char *root = getenv("NULLK64_ROOT");
    const char *p = path;
    size_t root_len;
    size_t n = 0u;

    if (p == NULL || out == NULL || cap < 4u) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (root == NULL || *root == '\0') {
        root = "/@";
    }
    root_len = strlen(root);
    while (root_len > 1u && root[root_len - 1u] == '/') {
        root_len--;
    }
    if (root_len + 2u >= cap) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    memcpy(out, root, root_len);
    n = root_len;

    if (*p == '@') {
        p++;
    }
    if (*p == '/') {
        p++;
    }
    if (*p == '\0') {
        out[n] = '\0';
        return NK_STATUS_OK;
    }
    if (n == 0u || out[n - 1u] != '/') {
        out[n++] = '/';
    }
    while (*p != '\0') {
        if (n + 1u >= cap) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
        if (*p == '.' && p[1] == '.' &&
            (p[2] == '\0' || p[2] == '/')) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
        out[n++] = *p++;
    }
    out[n] = '\0';
    return NK_STATUS_OK;
}

static uint64_t nk64_control_call(uint64_t nr, uint64_t a0, uint64_t a1,
                                  uint64_t a2, uint64_t a3)
{
    nk64_abi3_call_t call;

    nk64_ensure_ctl();
    if (g_ctl_fd < 0) {
        return (uint64_t)NK_STATUS_UNSUPPORTED;
    }
    memset(&call, 0, sizeof(call));
    call.magic = NK64_ABI3_MAGIC;
    call.version = NK64_ABI3_IO_VERSION;
    call.nr = (uint32_t)nr;
    call.args[0] = a0;
    call.args[1] = a1;
    call.args[2] = a2;
    call.args[3] = a3;
    if (ioctl(g_ctl_fd, NK64_ABI3_IOC_CALL, &call) != 0) {
        return (uint64_t)NK_STATUS_INTERNAL_ERROR;
    }
    return call.ret;
}

static uint64_t nk64_handle_from_fd(int fd)
{
    uint32_t i;

    pthread_mutex_lock(&g_nk_lock);
    for (i = 0u; i < NK64_LINUX_MAX_FILES; i++) {
        if (!g_files[i].used) {
            g_files[i].fd = fd;
            g_files[i].used = 1;
            pthread_mutex_unlock(&g_nk_lock);
            return (uint64_t)i + 2u; /* 0 = error, 1 = reserved */
        }
    }
    pthread_mutex_unlock(&g_nk_lock);
    (void)close(fd);
    return 0u;
}

static int nk64_fd_from_handle(uint64_t handle)
{
    uint32_t index;
    int fd;

    if (handle < 2u || handle >= (uint64_t)NK64_LINUX_MAX_FILES + 2u) {
        return -1;
    }
    index = (uint32_t)handle - 2u;
    pthread_mutex_lock(&g_nk_lock);
    if (!g_files[index].used) {
        fd = -1;
    } else {
        fd = g_files[index].fd;
    }
    pthread_mutex_unlock(&g_nk_lock);
    return fd;
}

static uint32_t nk64_linux_stat(const char *path, nk_vfs_stat_t *out)
{
    char host[NK_VFS_PATH_MAX];
    struct stat st;
    const char *name;
    const char *slash;
    size_t len;

    if (out == NULL || nk64_linux_map_path(path, host, sizeof(host)) !=
                       NK_STATUS_OK) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (stat(host, &st) != 0) {
        return NK_STATUS_NOT_FOUND;
    }
    slash = strrchr(host, '/');
    name = (slash != NULL) ? slash + 1 : host;
    len = strlen(name);
    if (len >= sizeof(out->name)) {
        len = sizeof(out->name) - 1u;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->name, name, len);
    out->type = S_ISDIR(st.st_mode) ? NK_VFS_DIR : NK_VFS_FILE;
    out->size = (uint32_t)st.st_size;
    out->id = (uint32_t)st.st_ino;
    return NK_STATUS_OK;
}

static uint64_t nk64_linux_readdir(const char *path, nk_vfs_dirent_t *out,
                                   uint32_t max)
{
    char host[NK_VFS_PATH_MAX];
    DIR *dir;
    struct dirent *ent;
    uint32_t count = 0u;

    if (out == NULL || max == 0u ||
        nk64_linux_map_path(path, host, sizeof(host)) != NK_STATUS_OK) {
        return 0u;
    }
    dir = opendir(host);
    if (dir == NULL) {
        return 0u;
    }
    while ((ent = readdir(dir)) != NULL && count < max) {
        size_t len;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        len = strlen(ent->d_name);
        if (len >= sizeof(out[count].name)) {
            len = sizeof(out[count].name) - 1u;
        }
        memset(&out[count], 0, sizeof(out[count]));
        memcpy(out[count].name, ent->d_name, len);
        out[count].type = ent->d_type == DT_DIR ? NK_VFS_DIR : NK_VFS_FILE;
        out[count].size = 0u;
        count++;
    }
    closedir(dir);
    return (uint64_t)count;
}

static void *nk64_task_trampoline(void *arg)
{
    nk64_task_slot_t *slot = arg;

    slot->entry();
    slot->running = 0;
    return NULL;
}

static uint64_t nk64_task_spawn(const char *name, void (*entry)(void),
                                uint64_t arg, uint32_t flags)
{
    nk64_task_slot_t *slot = NULL;
    pthread_attr_t attr;
    uint32_t i;

    (void)name;
    (void)flags;
    if (entry == NULL) {
        return 0u;
    }
    pthread_mutex_lock(&g_nk_lock);
    for (i = 0u; i < NK64_MAX_TASKS; i++) {
        if (!g_tasks[i].used) {
            slot = &g_tasks[i];
            memset(slot, 0, sizeof(*slot));
            slot->entry = entry;
            slot->arg = arg;
            slot->used = 1;
            slot->running = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_nk_lock);
    if (slot == NULL) {
        return 0u;
    }
    if (pthread_attr_init(&attr) != 0) {
        slot->used = 0;
        return 0u;
    }
    (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&slot->tid, &attr, nk64_task_trampoline, slot) != 0) {
        pthread_attr_destroy(&attr);
        slot->used = 0;
        return 0u;
    }
    pthread_attr_destroy(&attr);
    return (uint64_t)(i + 1u);
}

static uint32_t nk64_linux_task_state(uint32_t id)
{
    nk64_task_slot_t *slot;
    int rc;

    if (id == 0u || id > NK64_MAX_TASKS) {
        return NK_STATUS_NOT_FOUND;
    }
    slot = &g_tasks[id - 1u];
    if (!slot->used) {
        return NK_STATUS_NOT_FOUND;
    }
    rc = pthread_kill(slot->tid, 0);
    if (rc == ESRCH) {
        slot->running = 0;
        return NK_TASK_DEAD;
    }
    return NK_TASK_RUNNING;
}

static uint32_t nk64_load_elf(const uint8_t *image, uint32_t size,
                              const char *name)
{
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)(const void *)image;
    const Elf64_Phdr *ph;
    void (*entry)(void);
    uint32_t i;

    if (size < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 ||
        eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_ident[EI_DATA] != ELFDATA2LSB ||
        eh->e_type != ET_EXEC || eh->e_machine != EM_X86_64 ||
        eh->e_phentsize != sizeof(Elf64_Phdr) || eh->e_phnum == 0u) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if ((uint64_t)eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(Elf64_Phdr) >
        size) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    ph = (const Elf64_Phdr *)(const void *)(image + eh->e_phoff);
    for (i = 0u; i < eh->e_phnum; i++) {
        uint64_t start = ph[i].p_vaddr & ~((uint64_t)NK64_LINUX_PAGE_SIZE - 1u);
        uint64_t end = (ph[i].p_vaddr + ph[i].p_memsz +
                        (uint64_t)NK64_LINUX_PAGE_SIZE - 1u) &
                       ~((uint64_t)NK64_LINUX_PAGE_SIZE - 1u);
        uint64_t length = end - start;
        int prot = PROT_READ;
        void *map;

        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0u) {
            continue;
        }
        if ((uint64_t)ph[i].p_offset + ph[i].p_filesz > size ||
            ph[i].p_filesz > ph[i].p_memsz || length == 0u ||
            length > 0x1000000u) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
        if ((ph[i].p_flags & PF_W) != 0u) {
            prot = PROT_READ | PROT_WRITE;
        }
        if ((ph[i].p_flags & PF_X) != 0u) {
            prot |= PROT_EXEC;
        }
        /* Populate through a writable mapping, then publish the final ELF
         * protections. Read-only segments must not fault while copying. */
        map = mmap((void *)(uintptr_t)start, (size_t)length,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (map == MAP_FAILED) {
            return NK_STATUS_INTERNAL_ERROR;
        }
        memcpy((void *)(uintptr_t)ph[i].p_vaddr,
               image + ph[i].p_offset, (size_t)ph[i].p_filesz);
        if (ph[i].p_memsz > ph[i].p_filesz) {
            memset((void *)(uintptr_t)(ph[i].p_vaddr + ph[i].p_filesz), 0,
                   (size_t)(ph[i].p_memsz - ph[i].p_filesz));
        }
        if (mprotect((void *)(uintptr_t)start, (size_t)length, prot) != 0) {
            return NK_STATUS_INTERNAL_ERROR;
        }
    }
    *(void **)(&entry) = (void *)(uintptr_t)eh->e_entry;
    if (nk64_task_spawn(name, entry, 0u, 0u) == 0u) {
        return NK_STATUS_INTERNAL_ERROR;
    }
    return NK_STATUS_OK;
}

static uint32_t nk64_linux_exec_path(const char *path)
{
    char host[NK_VFS_PATH_MAX];
    uint8_t *image;
    struct stat st;
    int fd;
    ssize_t got;
    uint32_t len = 0u;
    uint32_t rc;

    if (nk64_linux_map_path(path, host, sizeof(host)) != NK_STATUS_OK) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    fd = open(host, O_RDONLY | O_CLOEXEC);
    if (fd < 0 || fstat(fd, &st) != 0 || st.st_size <= 0 ||
        st.st_size > NK64_LINUX_MAX_EXEC) {
        if (fd >= 0) {
            close(fd);
        }
        return NK_STATUS_NOT_FOUND;
    }
    image = malloc((size_t)st.st_size);
    if (image == NULL) {
        close(fd);
        return NK_STATUS_OUT_OF_MEMORY;
    }
    while (len < (uint32_t)st.st_size) {
        got = read(fd, image + len, (size_t)st.st_size - len);
        if (got <= 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    rc = nk64_load_elf(image, len, "exec");
    free(image);
    return rc;
}

static void nk64_linux_getinfo_local(nk64_info_t *out)
{
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    uint32_t i;

    memset(out, 0, sizeof(*out));
    out->abi_version = NULLK64_ABI_VERSION;
    out->magic = NULLK64_ABI_MAGIC;
    out->runlevel = NK_RUNLEVEL_READY;
    out->tick_hz = 100u;
    out->ticks = nk64_linux_ticks();
    out->task_count = 1u; /* the PID 1/init thread */
    pthread_mutex_lock(&g_nk_lock);
    for (i = 0u; i < NK64_MAX_TASKS; i++) {
        if (g_tasks[i].used) {
            out->task_count++;
        }
    }
    pthread_mutex_unlock(&g_nk_lock);
    out->flags = (uint32_t)NK_FEATURE_TASKS | (uint32_t)NK_FEATURE_MEMORY |
                 (uint32_t)NK_FEATURE_PREEMPT;
    out->hhdm_offset = 0u;
    if (pages > 0 && page_size > 0) {
        out->mem_total = (uint64_t)pages * (uint64_t)page_size;
    }
}

uint64_t nk64_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                      uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4;
    (void)a5;

    switch (n) {
    case NK64_SC_READ: {
        int fd = (a0 <= 1u) ? (int)a0 : nk64_fd_from_handle(a0);
        if (fd < 0) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        return (uint64_t)read(fd, (void *)(uintptr_t)a1, (size_t)a2);
    }
    case NK64_SC_WRITE: {
        int fd = (a0 <= 1u) ? (int)a0 : nk64_fd_from_handle(a0);
        if (fd < 0) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        return (uint64_t)write(fd, (const void *)(uintptr_t)a1, (size_t)a2);
    }
    case NK64_SC_OPEN: {
        char host[NK_VFS_PATH_MAX];
        int flags = (((uint32_t)a1 & NK64_OPEN_WRITE) != 0u) ? O_WRONLY :
                                                              O_RDONLY;
        int fd;

        if (nk64_linux_map_path((const char *)(uintptr_t)a0, host,
                                sizeof(host)) != NK_STATUS_OK) {
            return 0u;
        }
        fd = open(host, flags | O_CLOEXEC);
        return (fd < 0) ? 0u : nk64_handle_from_fd(fd);
    }
    case NK64_SC_CLOSE: {
        uint64_t index;
        int fd;

        if (a0 < 2u) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        index = a0 - 2u;
        if (index >= NK64_LINUX_MAX_FILES) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        pthread_mutex_lock(&g_nk_lock);
        fd = g_files[index].used ? g_files[index].fd : -1;
        g_files[index].used = 0;
        g_files[index].fd = -1;
        pthread_mutex_unlock(&g_nk_lock);
        if (fd < 0) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        close(fd);
        return (uint64_t)NK_STATUS_OK;
    }
    case NK64_SC_STAT:
        return nk64_linux_stat((const char *)(uintptr_t)a0,
                               (nk_vfs_stat_t *)(uintptr_t)a1);
    case NK64_SC_READDIR:
        return nk64_linux_readdir((const char *)(uintptr_t)a0,
                                  (nk_vfs_dirent_t *)(uintptr_t)a1,
                                  (uint32_t)a2);
    case NK64_SC_YIELD:
        (void)sched_yield();
        return (uint64_t)NK_STATUS_OK;
    case NK64_SC_SLEEP: {
        struct timespec ts;

        ts.tv_sec = (time_t)(a0 / 100u);
        ts.tv_nsec = (long)((a0 % 100u) * 10000000u);
        while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
            /* resume remaining time */
        }
        return (uint64_t)NK_STATUS_OK;
    }
    case NK64_SC_TICKS:
        return nk64_linux_ticks();
    case NK64_SC_GETINFO: {
        uint64_t rc;

        if (nk64_linux_control_fd() >= 0) {
            rc = nk64_control_call(NK64_SC_GETINFO, a0, 0u, 0u, 0u);
            if (rc == (uint64_t)NK_STATUS_OK) {
                return rc;
            }
        }
        if ((void *)(uintptr_t)a0 == NULL) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        nk64_linux_getinfo_local((nk64_info_t *)(uintptr_t)a0);
        return (uint64_t)NK_STATUS_OK;
    }
    case NK64_SC_EXIT:
        if (pthread_equal(pthread_self(), g_main_thread) != 0) {
            _exit((int)a0);
        }
        pthread_exit(NULL);
        return (uint64_t)NK_STATUS_OK; /* unreachable */
    case NK64_SC_EXEC:
        return nk64_linux_exec_path((const char *)(uintptr_t)a0);
    case NK64_SC_EXEC_MEM:
        return nk64_load_elf((const uint8_t *)(uintptr_t)a0, (uint32_t)a1,
                             "exec");
    case NK64_SC_MMAP: {
        uint64_t hint = a0;
        uint64_t len = a1;
        uint32_t flags = (uint32_t)a2;
        int mflags = MAP_PRIVATE | MAP_ANONYMOUS;
        void *addr;

        if (len == 0u || len > 0x40000000u) {
            return 0u;
        }
        if (hint == 0u && (flags & NK64_MM_SANDBOX) != 0u) {
            hint = 0x60000000u;
        }
        if (hint != 0u) {
            mflags |= MAP_FIXED_NOREPLACE;
        }
        addr = mmap((void *)(uintptr_t)hint, (size_t)len,
                    PROT_READ | PROT_WRITE, mflags, -1, 0);
        return (addr == MAP_FAILED) ? 0u : (uint64_t)(uintptr_t)addr;
    }
    case NK64_SC_LOG: {
        uint64_t rc = nk64_control_call(NK64_SC_LOG, (uint32_t)a0, a1, a2, 0u);

        if (rc != (uint64_t)NK_STATUS_OK) {
            ssize_t ignored = write(STDERR_FILENO,
                                    (const void *)(uintptr_t)a1, (size_t)a2);

            (void)ignored;
        }
        return (uint64_t)NK_STATUS_OK;
    }
    case NK64_SC_REBOOT:
        return (uint64_t)NK_STATUS_PERMISSION_DENIED;
    case NK64_SC_TASK_CREATE: {
        void (*entry)(void);

        *(void **)(&entry) = (void *)(uintptr_t)a1;
        return nk64_task_spawn((const char *)(uintptr_t)a0, entry, a2,
                               (uint32_t)a3);
    }
    case NK64_SC_TASK_STATE:
        return nk64_linux_task_state((uint32_t)a0);
    case NK64_SC_USB_READ:
    case NK64_SC_USB_WRITE:
    case NK64_SC_USB_REGISTER:
    case NK64_SC_FB_INFO:
    case NK64_SC_FB_MAP:
    case NK64_SC_INPUT_READ:
    case NK64_SC_IRQ_BIND:
    case NK64_SC_IPC_OPEN:
    case NK64_SC_IPC_SEND:
    case NK64_SC_IPC_RECV:
    case NK64_SC_IPC_REPLY:
        return nk64_control_call(n, a0, a1, a2, a3);
    default:
        return (uint64_t)NK_STATUS_UNSUPPORTED;
    }
}
