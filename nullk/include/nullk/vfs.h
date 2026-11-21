/*
 * null_kernel VFS: a small hierarchical filesystem rooted at "@/" (the '@'
 * prefix is the null_kernel namespace marker used by the bootloader config).
 *
 * Paths are normalised so that "@", "@/", "/" and "" all mean the root.
 * This layer is deliberately freestanding; the AMD64 kernel fills it from a
 * Limine module (USTAR archive) and the shell reads it back through callbacks.
 */
#ifndef NULLK_VFS_H
#define NULLK_VFS_H

#include <stdint.h>
#include "abi.h"

#define NK_VFS_NAME_MAX 32u
#define NK_VFS_PATH_MAX 192u
#define NK_VFS_MAX_NODES 160u
#define NK_VFS_FILE_MAX_BYTES 4194304u

enum {
    NK_VFS_DIR = 1u,
    NK_VFS_FILE = 2u
};

typedef struct {
    char name[NK_VFS_NAME_MAX];
    uint32_t type;
    uint32_t size;
} nk_vfs_dirent_t;

typedef struct {
    char name[NK_VFS_NAME_MAX];
    uint32_t type;
    uint32_t size;
    uint32_t id;
} nk_vfs_stat_t;

/* Lifecycle. nk_vfs_init() installs the standard @/data/system tree. */
void nk_vfs_init(void);
void nk_vfs_reset(void);

/* Normalise a path into an absolute '@/' form, e.g. "@foo/../bar" -> "@/bar".
 * Returns NK_STATUS_OK or NK_STATUS_INVALID_ARGUMENT. */
nk_status_t nk_vfs_normalize(const char *path, char *out, uint32_t cap);

/* Directory/file manipulation. All take normalised or '@' prefixed paths. */
nk_status_t nk_vfs_mkdir(const char *path);
nk_status_t nk_vfs_write_file(const char *path, const void *data, uint32_t size);
nk_status_t nk_vfs_append_file(const char *path, const void *data, uint32_t size);
nk_status_t nk_vfs_read_file(const char *path, uint32_t offset, void *buf,
                             uint32_t cap, uint32_t *out_len);
nk_status_t nk_vfs_stat(const char *path, nk_vfs_stat_t *out);
nk_status_t nk_vfs_readdir(const char *path, nk_vfs_dirent_t *out,
                           uint32_t max, uint32_t *count);
nk_status_t nk_vfs_unlink(const char *path);

/* Load a USTAR archive into the tree. Entries are prefixed with @/. */
nk_status_t nk_vfs_load_tar(const uint8_t *blob, uint32_t size);

/* Count of live nodes and total bytes stored (for info shell command). */
void nk_vfs_usage(uint32_t *nodes, uint32_t *bytes);

#endif /* NULLK_VFS_H */
