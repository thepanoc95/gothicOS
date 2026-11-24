/*
 * null_kernel VFS - a tiny static hierarchical filesystem.
 *
 * All metadata lives in one fixed node array and all file bytes live in a
 * fixed 1 MiB pool. No heap, no libc. Paths are normalised to the canonical
 * "@/..." form before any lookup so the kernel and the hosted shells see the
 * same namespace regardless of how a caller spells a path.
 */
#include "nullk/vfs.h"

#include <stddef.h>

/* The AMD64 test initrd ships the same userspace ELF as init, shell and
 * USB payload, so the pool must hold a few megabytes. */
#define NK_VFS_POOL_SIZE (4u * 1024u * 1024u)
#define NK_VFS_NO_NODE   0xffffffffu

typedef struct {
    char name[NK_VFS_NAME_MAX];
    uint32_t type;
    uint32_t parent;
    uint32_t first_child;
    uint32_t next_sibling;
    uint32_t pool_off;
    uint32_t size;
    uint32_t used;
} nk_vfs_node_t;

static nk_vfs_node_t g_nodes[NK_VFS_MAX_NODES];
static uint8_t g_pool[NK_VFS_POOL_SIZE];
static uint32_t g_pool_used;
static uint32_t g_node_count;

/* ------------------------------------------------------------------ */
/* Small local libc replacements (must stay static: no exported symbols). */
/* ------------------------------------------------------------------ */

static void nk_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        d[i] = s[i];
    }
}

static void nk_memset(void *dst, int value, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        d[i] = (uint8_t)value;
    }
}

static uint32_t nk_strlen(const char *s)
{
    uint32_t n = 0u;

    while ((s != (const char *)0) && (s[n] != '\0')) {
        n++;
    }
    return n;
}

static int nk_strcmp(const char *a, const char *b)
{
    while ((*a != '\0') && (*a == *b)) {
        a++;
        b++;
    }
    if (*a == *b) {
        return 0;
    }
    return (*a < *b) ? -1 : 1;
}

static void nk_strcpy(char *dst, const char *src)
{
    while (*src != '\0') {
        *dst++ = *src++;
    }
    *dst = '\0';
}

/* ------------------------------------------------------------------ */
/* Path normalisation.                                                 */
/* ------------------------------------------------------------------ */

nk_status_t nk_vfs_normalize(const char *path, char *out, uint32_t cap)
{
    char parts[NK_VFS_PATH_MAX + 2u];
    uint32_t marks[NK_VFS_PATH_MAX / 2u + 2u];
    uint32_t depth = 0u;
    uint32_t used = 0u;
    uint32_t in_len = 0u;
    uint32_t i = 0u;
    uint32_t need;

    if ((path == (const char *)0) || (out == (const char *)0) || (cap == 0u)) {
        return NK_STATUS_INVALID_ARGUMENT;
    }

    while (path[in_len] != '\0') {
        in_len++;
        if (in_len > NK_VFS_PATH_MAX) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
    }

    /* The '@' marker is optional and may be directly followed by a name. */
    if (path[0] == '@') {
        i = 1u;
    }

    while (i < in_len) {
        uint32_t start;
        uint32_t len;
        uint32_t j;

        while ((i < in_len) && (path[i] == '/')) {
            i++;
        }
        if (i >= in_len) {
            break;
        }
        start = i;
        while ((i < in_len) && (path[i] != '/')) {
            i++;
        }
        len = i - start;

        if ((len == 1u) && (path[start] == '.')) {
            continue;
        }
        if ((len == 2u) && (path[start] == '.') && (path[start + 1u] == '.')) {
            if (depth == 0u) {
                return NK_STATUS_INVALID_ARGUMENT; /* escapes the root */
            }
            depth--;
            used = marks[depth];
            continue;
        }

        /* Remember the length before this component (and its separator) so
         * that ".." can pop exactly one component. */
        marks[depth++] = used;
        if ((used != 0u) && (used < (uint32_t)sizeof(parts))) {
            parts[used++] = '/';
        }
        if ((used + len) > (uint32_t)sizeof(parts)) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
        for (j = 0u; j < len; j++) {
            parts[used++] = path[start + j];
        }
    }

    need = 2u + used + 1u; /* "@/" + parts + NUL */
    if ((need > cap) || ((2u + used) > NK_VFS_PATH_MAX)) {
        return NK_STATUS_OUT_OF_MEMORY;
    }
    out[0] = '@';
    out[1] = '/';
    if (used != 0u) {
        nk_memcpy(out + 2, parts, used);
    }
    out[2u + used] = '\0';
    return NK_STATUS_OK;
}

/* ------------------------------------------------------------------ */
/* Node helpers.                                                       */
/* ------------------------------------------------------------------ */

static void nk_vfs_set_name(uint32_t index, const char *name, uint32_t len)
{
    uint32_t i;

    if (len >= NK_VFS_NAME_MAX) {
        len = NK_VFS_NAME_MAX - 1u;
    }
    for (i = 0u; i < len; i++) {
        g_nodes[index].name[i] = name[i];
    }
    g_nodes[index].name[len] = '\0';
}

static uint32_t nk_vfs_alloc_node(void)
{
    uint32_t i;

    for (i = 0u; i < NK_VFS_MAX_NODES; i++) {
        if (g_nodes[i].used == 0u) {
            nk_memset(&g_nodes[i], 0, (uint32_t)sizeof(g_nodes[i]));
            g_nodes[i].parent = NK_VFS_NO_NODE;
            g_nodes[i].first_child = NK_VFS_NO_NODE;
            g_nodes[i].next_sibling = NK_VFS_NO_NODE;
            g_nodes[i].used = 1u;
            g_node_count++;
            return i;
        }
    }
    return NK_VFS_NO_NODE;
}

static void nk_vfs_link_child(uint32_t parent, uint32_t child)
{
    uint32_t cursor = g_nodes[parent].first_child;

    g_nodes[child].parent = parent;
    g_nodes[child].next_sibling = NK_VFS_NO_NODE;
    if (cursor == NK_VFS_NO_NODE) {
        g_nodes[parent].first_child = child;
        return;
    }
    while (g_nodes[cursor].next_sibling != NK_VFS_NO_NODE) {
        cursor = g_nodes[cursor].next_sibling;
    }
    g_nodes[cursor].next_sibling = child;
}

static void nk_vfs_unlink_child(uint32_t parent, uint32_t child)
{
    uint32_t cursor = g_nodes[parent].first_child;
    uint32_t previous = NK_VFS_NO_NODE;

    while (cursor != NK_VFS_NO_NODE) {
        if (cursor == child) {
            if (previous == NK_VFS_NO_NODE) {
                g_nodes[parent].first_child = g_nodes[child].next_sibling;
            } else {
                g_nodes[previous].next_sibling = g_nodes[child].next_sibling;
            }
            return;
        }
        previous = cursor;
        cursor = g_nodes[cursor].next_sibling;
    }
}

static uint32_t nk_vfs_find_child(uint32_t parent, const char *name)
{
    uint32_t cursor = g_nodes[parent].first_child;

    while (cursor != NK_VFS_NO_NODE) {
        if (nk_strcmp(g_nodes[cursor].name, name) == 0) {
            return cursor;
        }
        cursor = g_nodes[cursor].next_sibling;
    }
    return NK_VFS_NO_NODE;
}

/* Return the next path component, or NULL at the end. `p` points at the
 * separator that stopped the scan so it can be passed back on the next call. */
static const char *nk_vfs_next_component(const char *p, char *buf, uint32_t cap)
{
    uint32_t len;
    uint32_t i;

    while (*p == '/') {
        p++;
    }
    if (*p == '\0') {
        return (const char *)0;
    }
    len = 0u;
    while ((p[len] != '\0') && (p[len] != '/')) {
        len++;
    }
    if (len >= cap) {
        len = cap - 1u;
    }
    for (i = 0u; i < len; i++) {
        buf[i] = p[i];
    }
    buf[len] = '\0';
    return p + len;
}

static nk_status_t nk_vfs_find(const char *path, uint32_t *index_out)
{
    char norm[NK_VFS_PATH_MAX];
    nk_status_t status;
    uint32_t current = 0u; /* root */
    const char *p;

    status = nk_vfs_normalize(path, norm, (uint32_t)sizeof(norm));
    if (status != NK_STATUS_OK) {
        return status;
    }
    p = norm + 2;
    for (;;) {
        char component[NK_VFS_NAME_MAX];
        uint32_t child;

        p = nk_vfs_next_component(p, component, (uint32_t)sizeof(component));
        if (p == (const char *)0) {
            break;
        }
        child = nk_vfs_find_child(current, component);
        if (child == NK_VFS_NO_NODE) {
            return NK_STATUS_NOT_FOUND;
        }
        current = child;
    }
    if (index_out != (uint32_t *)0) {
        *index_out = current;
    }
    return NK_STATUS_OK;
}

/* Create every directory on `path` and return the final directory index. */
static nk_status_t nk_vfs_ensure_dir(const char *path, uint32_t *index_out)
{
    char norm[NK_VFS_PATH_MAX];
    nk_status_t status;
    uint32_t current = 0u;
    const char *p;

    status = nk_vfs_normalize(path, norm, (uint32_t)sizeof(norm));
    if (status != NK_STATUS_OK) {
        return status;
    }
    p = norm + 2;
    for (;;) {
        char component[NK_VFS_NAME_MAX];
        uint32_t child;
        uint32_t fresh;

        p = nk_vfs_next_component(p, component, (uint32_t)sizeof(component));
        if (p == (const char *)0) {
            break;
        }
        child = nk_vfs_find_child(current, component);
        if (child != NK_VFS_NO_NODE) {
            if (g_nodes[child].type != NK_VFS_DIR) {
                return NK_STATUS_INVALID_ARGUMENT;
            }
            current = child;
            continue;
        }
        fresh = nk_vfs_alloc_node();
        if (fresh == NK_VFS_NO_NODE) {
            return NK_STATUS_OUT_OF_MEMORY;
        }
        nk_vfs_set_name(fresh, component, nk_strlen(component));
        g_nodes[fresh].type = NK_VFS_DIR;
        nk_vfs_link_child(current, fresh);
        current = fresh;
    }
    if (index_out != (uint32_t *)0) {
        *index_out = current;
    }
    return NK_STATUS_OK;
}

static void nk_vfs_split(const char *norm, char *parent, char *leaf,
                         uint32_t leaf_cap)
{
    uint32_t len = nk_strlen(norm);
    uint32_t last = 0u;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        if (norm[i] == '/') {
            last = i;
        }
    }
    if (last < 2u) {
        nk_strcpy(parent, "@/");
    } else {
        for (i = 0u; i < last; i++) {
            parent[i] = norm[i];
        }
        parent[last] = '\0';
    }
    {
        const char *s = norm + last + 1u;
        uint32_t n = 0u;

        while ((*s != '\0') && (n < (leaf_cap - 1u))) {
            leaf[n++] = *s++;
        }
        leaf[n] = '\0';
    }
}

static uint32_t nk_vfs_pool_alloc(uint32_t size)
{
    uint32_t offset;

    if (size > (NK_VFS_POOL_SIZE - g_pool_used)) {
        return NK_VFS_NO_NODE;
    }
    offset = g_pool_used;
    g_pool_used += size;
    return offset;
}

/* ------------------------------------------------------------------ */
/* Public lifecycle.                                                   */
/* ------------------------------------------------------------------ */

void nk_vfs_reset(void)
{
    nk_memset(g_nodes, 0, (uint32_t)sizeof(g_nodes));
    nk_memset(g_pool, 0, (uint32_t)sizeof(g_pool));
    g_pool_used = 0u;
    g_node_count = 1u;
    g_nodes[0].used = 1u;
    g_nodes[0].type = NK_VFS_DIR;
    g_nodes[0].parent = NK_VFS_NO_NODE;
    g_nodes[0].first_child = NK_VFS_NO_NODE;
    g_nodes[0].next_sibling = NK_VFS_NO_NODE;
    nk_vfs_set_name(0u, "@/", 2u);
}

void nk_vfs_init(void)
{
    nk_vfs_reset();
    (void)nk_vfs_ensure_dir("@/data", (uint32_t *)0);
    (void)nk_vfs_ensure_dir("@/data/system", (uint32_t *)0);
    (void)nk_vfs_ensure_dir("@/data/user", (uint32_t *)0);
    (void)nk_vfs_ensure_dir("@/data/user/bin", (uint32_t *)0);
    (void)nk_vfs_ensure_dir("@/usb", (uint32_t *)0);
}

/* ------------------------------------------------------------------ */
/* Files and directories.                                              */
/* ------------------------------------------------------------------ */

nk_status_t nk_vfs_mkdir(const char *path)
{
    char norm[NK_VFS_PATH_MAX];
    char parent[NK_VFS_PATH_MAX];
    char leaf[NK_VFS_NAME_MAX];
    nk_status_t status;
    uint32_t parent_index;
    uint32_t existing;

    status = nk_vfs_normalize(path, norm, (uint32_t)sizeof(norm));
    if (status != NK_STATUS_OK) {
        return status;
    }
    nk_vfs_split(norm, parent, leaf, (uint32_t)sizeof(leaf));
    if (leaf[0] == '\0') {
        return NK_STATUS_INVALID_ARGUMENT; /* root already exists */
    }
    status = nk_vfs_ensure_dir(parent, &parent_index);
    if (status != NK_STATUS_OK) {
        return status;
    }
    existing = nk_vfs_find_child(parent_index, leaf);
    if (existing != NK_VFS_NO_NODE) {
        return (g_nodes[existing].type == NK_VFS_DIR) ? NK_STATUS_OK
                                                      : NK_STATUS_INVALID_ARGUMENT;
    }
    {
        uint32_t fresh = nk_vfs_alloc_node();

        if (fresh == NK_VFS_NO_NODE) {
            return NK_STATUS_OUT_OF_MEMORY;
        }
        nk_vfs_set_name(fresh, leaf, nk_strlen(leaf));
        g_nodes[fresh].type = NK_VFS_DIR;
        nk_vfs_link_child(parent_index, fresh);
    }
    return NK_STATUS_OK;
}

nk_status_t nk_vfs_write_file(const char *path, const void *data, uint32_t size)
{
    char norm[NK_VFS_PATH_MAX];
    char parent[NK_VFS_PATH_MAX];
    char leaf[NK_VFS_NAME_MAX];
    nk_status_t status;
    uint32_t parent_index;
    uint32_t index;
    uint32_t offset;
    uint32_t i;

    if ((size != 0u) && (data == (const void *)0)) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (size > NK_VFS_FILE_MAX_BYTES) {
        return NK_STATUS_OUT_OF_MEMORY;
    }
    status = nk_vfs_normalize(path, norm, (uint32_t)sizeof(norm));
    if (status != NK_STATUS_OK) {
        return status;
    }
    nk_vfs_split(norm, parent, leaf, (uint32_t)sizeof(leaf));
    if (leaf[0] == '\0') {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    status = nk_vfs_ensure_dir(parent, &parent_index);
    if (status != NK_STATUS_OK) {
        return status;
    }

    index = nk_vfs_find_child(parent_index, leaf);
    if (index != NK_VFS_NO_NODE) {
        if (g_nodes[index].type != NK_VFS_FILE) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
        /* Overwrite silently. Reuse the old allocation when it fits. */
        if (size <= g_nodes[index].size) {
            offset = g_nodes[index].pool_off;
            for (i = 0u; i < size; i++) {
                g_pool[offset + i] = ((const uint8_t *)data)[i];
            }
            g_nodes[index].size = size;
            return NK_STATUS_OK;
        }
    } else {
        index = nk_vfs_alloc_node();
        if (index == NK_VFS_NO_NODE) {
            return NK_STATUS_OUT_OF_MEMORY;
        }
        nk_vfs_set_name(index, leaf, nk_strlen(leaf));
        g_nodes[index].type = NK_VFS_FILE;
        nk_vfs_link_child(parent_index, index);
    }

    offset = nk_vfs_pool_alloc(size);
    if (offset == NK_VFS_NO_NODE) {
        return NK_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0u; i < size; i++) {
        g_pool[offset + i] = ((const uint8_t *)data)[i];
    }
    g_nodes[index].pool_off = offset;
    g_nodes[index].size = size;
    return NK_STATUS_OK;
}

nk_status_t nk_vfs_append_file(const char *path, const void *data, uint32_t size)
{
    uint32_t index;
    nk_status_t status;
    uint32_t old_size;
    uint32_t total;
    uint32_t offset;
    uint32_t i;

    if ((size != 0u) && (data == (const void *)0)) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    status = nk_vfs_find(path, &index);
    if (status != NK_STATUS_OK) {
        return status;
    }
    if (g_nodes[index].type != NK_VFS_FILE) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    old_size = g_nodes[index].size;
    if (size > (NK_VFS_FILE_MAX_BYTES - old_size)) {
        return NK_STATUS_OUT_OF_MEMORY;
    }
    total = old_size + size;

    /* Grow in place when this file owns the tail of the pool. */
    if ((g_nodes[index].pool_off + old_size) == g_pool_used) {
        if (total > (NK_VFS_POOL_SIZE - g_nodes[index].pool_off)) {
            return NK_STATUS_OUT_OF_MEMORY;
        }
        offset = g_nodes[index].pool_off;
        g_pool_used = offset + total;
    } else {
        offset = nk_vfs_pool_alloc(total);
        if (offset == NK_VFS_NO_NODE) {
            return NK_STATUS_OUT_OF_MEMORY;
        }
        for (i = 0u; i < old_size; i++) {
            g_pool[offset + i] = g_pool[g_nodes[index].pool_off + i];
        }
    }
    for (i = 0u; i < size; i++) {
        g_pool[offset + old_size + i] = ((const uint8_t *)data)[i];
    }
    g_nodes[index].pool_off = offset;
    g_nodes[index].size = total;
    return NK_STATUS_OK;
}

nk_status_t nk_vfs_read_file(const char *path, uint32_t offset, void *buf,
                             uint32_t cap, uint32_t *out_len)
{
    uint32_t index;
    nk_status_t status;
    uint32_t available;
    uint32_t count;
    uint32_t i;

    if ((buf == (void *)0) || (out_len == (uint32_t *)0)) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    *out_len = 0u;
    status = nk_vfs_find(path, &index);
    if (status != NK_STATUS_OK) {
        return status;
    }
    if (g_nodes[index].type != NK_VFS_FILE) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (offset > g_nodes[index].size) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    available = g_nodes[index].size - offset;
    count = (available < cap) ? available : cap;
    for (i = 0u; i < count; i++) {
        ((uint8_t *)buf)[i] = g_pool[g_nodes[index].pool_off + offset + i];
    }
    *out_len = count;
    return NK_STATUS_OK;
}

nk_status_t nk_vfs_stat(const char *path, nk_vfs_stat_t *out)
{
    uint32_t index;
    nk_status_t status;

    if (out == (nk_vfs_stat_t *)0) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    status = nk_vfs_find(path, &index);
    if (status != NK_STATUS_OK) {
        return status;
    }
    nk_strcpy(out->name, g_nodes[index].name);
    out->type = g_nodes[index].type;
    out->size = g_nodes[index].size;
    out->id = index;
    return NK_STATUS_OK;
}

nk_status_t nk_vfs_readdir(const char *path, nk_vfs_dirent_t *out,
                           uint32_t max, uint32_t *count)
{
    uint32_t index;
    nk_status_t status;
    uint32_t child;
    uint32_t n = 0u;

    if ((out == (nk_vfs_dirent_t *)0) || (count == (uint32_t *)0)) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    *count = 0u;
    status = nk_vfs_find(path, &index);
    if (status != NK_STATUS_OK) {
        return status;
    }
    if (g_nodes[index].type != NK_VFS_DIR) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    child = g_nodes[index].first_child;
    while ((child != NK_VFS_NO_NODE) && (n < max)) {
        nk_strcpy(out[n].name, g_nodes[child].name);
        out[n].type = g_nodes[child].type;
        out[n].size = g_nodes[child].size;
        n++;
        child = g_nodes[child].next_sibling;
    }
    *count = n;
    return NK_STATUS_OK;
}

nk_status_t nk_vfs_unlink(const char *path)
{
    char norm[NK_VFS_PATH_MAX];
    nk_status_t status;
    uint32_t index;
    uint32_t parent;

    status = nk_vfs_normalize(path, norm, (uint32_t)sizeof(norm));
    if (status != NK_STATUS_OK) {
        return status;
    }
    if (nk_strcmp(norm, "@/") == 0) {
        return NK_STATUS_INVALID_ARGUMENT; /* cannot remove the root */
    }
    status = nk_vfs_find(norm, &index);
    if (status != NK_STATUS_OK) {
        return status;
    }
    if ((g_nodes[index].type == NK_VFS_DIR) &&
        (g_nodes[index].first_child != NK_VFS_NO_NODE)) {
        return NK_STATUS_BUSY; /* only empty directories can be removed */
    }
    parent = g_nodes[index].parent;
    if (parent == NK_VFS_NO_NODE) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    nk_vfs_unlink_child(parent, index);
    g_nodes[index].used = 0u;
    if (g_node_count > 0u) {
        g_node_count--;
    }
    return NK_STATUS_OK;
}

/* ------------------------------------------------------------------ */
/* USTAR loader.                                                       */
/* ------------------------------------------------------------------ */

static uint64_t nk_vfs_tar_octal(const uint8_t *p, uint32_t n)
{
    uint64_t value = 0u;
    uint32_t i = 0u;

    while ((i < n) && ((p[i] == ' ') || (p[i] == '\0'))) {
        i++;
    }
    while ((i < n) && (p[i] >= '0') && (p[i] <= '7')) {
        value = (value * 8u) + (uint64_t)(p[i] - '0');
        i++;
    }
    return value;
}

static uint32_t nk_vfs_tar_cstr(const uint8_t *p, uint32_t n, char *out,
                                uint32_t cap)
{
    uint32_t i = 0u;

    while ((i < n) && (p[i] != '\0') && ((i + 1u) < cap)) {
        out[i] = (char)p[i];
        i++;
    }
    out[i] = '\0';
    return i;
}

nk_status_t nk_vfs_load_tar(const uint8_t *blob, uint32_t size)
{
    uint32_t offset = 0u;

    if (blob == (const uint8_t *)0) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    while ((offset + 512u) <= size) {
        const uint8_t *hdr = blob + offset;
        uint8_t type = hdr[156];
        uint64_t file_size = nk_vfs_tar_octal(hdr + 124, 12);
        uint64_t blocks = (file_size + 511u) / 512u;
        uint64_t payload_bytes;
        uint32_t payload_off;
        char raw[NK_VFS_PATH_MAX];
        char name[100];
        char prefix[155];
        uint32_t name_len;
        uint32_t prefix_len;
        uint32_t path_i = 0u;
        uint32_t i;

        /* A zero block terminates the archive. */
        {
            int all_zero = 1;

            for (i = 0u; i < 512u; i++) {
                if (hdr[i] != 0u) {
                    all_zero = 0;
                    break;
                }
            }
            if (all_zero) {
                break;
            }
        }

        if (blocks > (uint64_t)((size - offset - 512u) / 512u)) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
        payload_bytes = blocks * 512u;
        payload_off = offset + 512u;

        /* PAX/GNU metadata and other non-file entries are skipped cleanly. */
        if ((type == 'x') || (type == 'X') || (type == 'g') ||
            (type == 'L') || (type == 'K') || (type == '2') ||
            (type == '3') || (type == '4') || (type == '6') ||
            (type == '7')) {
            offset = payload_off + (uint32_t)payload_bytes;
            continue;
        }

        name_len = nk_vfs_tar_cstr(hdr, 100u, name, (uint32_t)sizeof(name));
        prefix_len = nk_vfs_tar_cstr(hdr + 345, 155u, prefix,
                                     (uint32_t)sizeof(prefix));
        if ((prefix_len != 0u) && (name_len != 0u)) {
            for (i = 0u; (i < prefix_len) && (path_i < (NK_VFS_PATH_MAX - 1u));
                 i++) {
                raw[path_i++] = prefix[i];
            }
            if (path_i < (NK_VFS_PATH_MAX - 1u)) {
                raw[path_i++] = '/';
            }
        }
        for (i = 0u; (i < name_len) && (path_i < (NK_VFS_PATH_MAX - 1u)); i++) {
            raw[path_i++] = name[i];
        }
        raw[path_i] = '\0';

        /* Strip "./" prefixes and the namespace marker. */
        while ((raw[0] == '.') && (raw[1] == '/')) {
            uint32_t j;

            for (j = 0u; raw[j + 2u] != '\0'; j++) {
                raw[j] = raw[j + 2u];
            }
            raw[j] = '\0';
        }
        if (raw[0] == '@') {
            uint32_t j;

            for (j = 0u; raw[j + 1u] != '\0'; j++) {
                raw[j] = raw[j + 1u];
            }
            raw[j] = '\0';
        }
        if (raw[0] == '\0') {
            offset = payload_off + (uint32_t)payload_bytes;
            continue;
        }

        /* USTAR directory names conventionally end in '/'. */
        if ((raw[0] != '\0') && (raw[nk_strlen(raw) - 1u] == '/')) {
            raw[nk_strlen(raw) - 1u] = '\0';
        }

        if (type == '5') {
            if (nk_vfs_ensure_dir(raw, (uint32_t *)0) != NK_STATUS_OK) {
                return NK_STATUS_INVALID_ARGUMENT;
            }
        } else if ((type == '0') || (type == '\0')) {
            if (file_size > (uint64_t)NK_VFS_FILE_MAX_BYTES) {
                return NK_STATUS_OUT_OF_MEMORY;
            }
            if (nk_vfs_write_file(raw, blob + payload_off,
                                  (uint32_t)file_size) != NK_STATUS_OK) {
                return NK_STATUS_INVALID_ARGUMENT;
            }
        }

        offset = payload_off + (uint32_t)payload_bytes;
    }
    return NK_STATUS_OK;
}

void nk_vfs_usage(uint32_t *nodes, uint32_t *bytes)
{
    if (nodes != (uint32_t *)0) {
        *nodes = g_node_count;
    }
    if (bytes != (uint32_t *)0) {
        *bytes = g_pool_used;
    }
}
