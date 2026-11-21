/*
 * SecureKernel type-enforcement policy and audit engine.
 *
 * A lightweight SELinux-style model for ring 3: tasks present a subject label,
 * files are labelled by the longest matching path prefix, and rules grant or
 * deny operations between subject and object contexts. Unmatched requests fail
 * closed (default DENY). All storage is static and bounded; no libc, heap or
 * platform headers are used so this links into both the host tests and the
 * freestanding userspace image.
 */
#include "engine.h"

namespace nksecure {

namespace {

const char *const kOpNames[NK_SECURE_OP_COUNT] = {
    "exec", "read", "write", "load"
};

nk_secure_policy_t g_policy;
int g_policy_ready = 0;
int g_policy_locked = 0;

nk_secure_audit_t g_audit[NK_SECURE_MAX_AUDIT];
uint32_t g_audit_start;
uint32_t g_audit_count;
uint32_t g_audit_sequence;

void byte_zero(void *dst, uint32_t len)
{
    uint8_t *p = (uint8_t *)dst;

    for (uint32_t i = 0u; i < len; i++) {
        p[i] = 0u;
    }
}

uint32_t str_len(const char *s)
{
    uint32_t n = 0u;

    if (s == 0) {
        return 0u;
    }
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

void str_copy(char *dst, uint32_t cap, const char *src)
{
    uint32_t n = 0u;

    if (dst == 0 || cap == 0u) {
        return;
    }
    if (src != 0) {
        while (n + 1u < cap && src[n] != '\0') {
            dst[n] = src[n];
            n++;
        }
    }
    dst[n] = '\0';
}

void str_append(char *dst, uint32_t cap, const char *src)
{
    uint32_t at = str_len(dst);
    uint32_t n = 0u;

    if (dst == 0 || cap == 0u || src == 0) {
        return;
    }
    while (at + 1u < cap && src[n] != '\0') {
        dst[at++] = src[n++];
    }
    dst[at] = '\0';
}

int str_equal(const char *a, const char *b)
{
    uint32_t i = 0u;

    if (a == 0 || b == 0) {
        return 0;
    }
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i] ? 1 : 0;
}

int str_has_prefix(const char *text, const char *prefix)
{
    uint32_t i = 0u;

    if (text == 0 || prefix == 0) {
        return 0;
    }
    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

void label_set(nk_secure_label_t *label, const char *name)
{
    str_copy(label->name, (uint32_t)sizeof(label->name), name);
}

int label_valid(const nk_secure_label_t *label)
{
    return str_len(label->name) != 0u ? 1 : 0;
}

int operation_valid(uint32_t operations)
{
    if (operations == 0u) {
        return 0;
    }
    return (operations & ~((1u << NK_SECURE_OP_COUNT) - 1u)) == 0u ? 1 : 0;
}

int decision_valid(uint8_t decision)
{
    return decision == (uint8_t)NK_SECURE_DECISION_ALLOW ||
           decision == (uint8_t)NK_SECURE_DECISION_DENY ? 1 : 0;
}

int add_rule(nk_secure_policy_t *policy, const char *subject,
             const char *object, uint32_t operations, uint8_t decision)
{
    nk_secure_rule_t *rule;

    if (policy->rule_count >= (uint32_t)NK_SECURE_MAX_RULES) {
        return 0;
    }
    rule = &policy->rules[policy->rule_count++];
    byte_zero(rule, (uint32_t)sizeof(*rule));
    label_set(&rule->subject, subject);
    label_set(&rule->object, object);
    rule->operations = operations;
    rule->decision = decision;
    return 1;
}

int add_path(nk_secure_policy_t *policy, const char *prefix,
             const char *object)
{
    nk_secure_path_rule_t *path;
    uint32_t len = str_len(prefix);

    if (policy->path_count >= (uint32_t)NK_SECURE_MAX_PATH_RULES ||
        len == 0u || len >= (uint32_t)NK_SECURE_PATH_MAX) {
        return 0;
    }
    path = &policy->paths[policy->path_count++];
    byte_zero(path, (uint32_t)sizeof(*path));
    str_copy(path->prefix, (uint32_t)sizeof(path->prefix), prefix);
    label_set(&path->object, object);
    return 1;
}

void install_policy(const nk_secure_policy_t *policy)
{
    g_policy = *policy;
    g_policy_ready = 1;
}

void ensure_policy(void)
{
    nk_secure_policy_t builtin;

    if (g_policy_ready) {
        return;
    }
    if (policy_builtin(&builtin) == 0) {
        install_policy(&builtin);
    }
}

const char *op_name(uint32_t operation)
{
    if (operation < (uint32_t)NK_SECURE_OP_COUNT) {
        return kOpNames[operation];
    }
    return "unknown";
}

void policy_detail(char *dst, uint32_t cap, const char *subject,
                   const char *object, uint32_t operation, int allowed)
{
    str_copy(dst, cap, allowed ? "allowed by policy: " : "blocked by policy: ");
    str_append(dst, cap, subject);
    str_append(dst, cap, "/");
    str_append(dst, cap, object);
    str_append(dst, cap, " op=");
    str_append(dst, cap, op_name(operation));
}

} /* namespace */

int policy_builtin(nk_secure_policy_t *out)
{
    nk_secure_policy_t policy;
    const uint32_t allow = (uint32_t)NK_SECURE_DECISION_ALLOW;

    if (out == 0) {
        return 1;
    }
    byte_zero(&policy, (uint32_t)sizeof(policy));
    policy.default_decision = (uint8_t)NK_SECURE_DECISION_DENY;

    /* Longest matching prefix wins. System images are never Exec-able by the
     * interactive shell; USB payloads are loadable only by the USB service. */
    add_path(&policy, "@/data/system/", NK_SECURE_OBJECT_SYSTEM);
    add_path(&policy, "@/data/user/", NK_SECURE_OBJECT_USER);
    add_path(&policy, "@/usb/", NK_SECURE_OBJECT_USB);
    add_path(&policy, "@/data/", NK_SECURE_OBJECT_CONFIG);

    add_rule(&policy, NK_SECURE_SUBJECT_INIT, NK_SECURE_OBJECT_SYSTEM,
             NK_SECURE_OP_BIT(NK_SECURE_OP_READ), allow);
    add_rule(&policy, NK_SECURE_SUBJECT_INIT, NK_SECURE_OBJECT_CONFIG,
             NK_SECURE_OP_BIT(NK_SECURE_OP_READ), allow);
    add_rule(&policy, NK_SECURE_SUBJECT_SHELL, NK_SECURE_OBJECT_SYSTEM,
             NK_SECURE_OP_BIT(NK_SECURE_OP_READ), allow);
    add_rule(&policy, NK_SECURE_SUBJECT_SHELL, NK_SECURE_OBJECT_CONFIG,
             NK_SECURE_OP_BIT(NK_SECURE_OP_READ), allow);
    add_rule(&policy, NK_SECURE_SUBJECT_SHELL, NK_SECURE_OBJECT_USER,
             NK_SECURE_OP_BIT(NK_SECURE_OP_READ) |
             NK_SECURE_OP_BIT(NK_SECURE_OP_EXEC), allow);
    add_rule(&policy, NK_SECURE_SUBJECT_SHELL, NK_SECURE_OBJECT_USB,
             NK_SECURE_OP_BIT(NK_SECURE_OP_READ) |
             NK_SECURE_OP_BIT(NK_SECURE_OP_LOAD), allow);
    add_rule(&policy, NK_SECURE_SUBJECT_SERVICE, NK_SECURE_OBJECT_SYSTEM,
             NK_SECURE_OP_BIT(NK_SECURE_OP_READ), allow);
    add_rule(&policy, NK_SECURE_SUBJECT_SERVICE, NK_SECURE_OBJECT_USB,
             NK_SECURE_OP_BIT(NK_SECURE_OP_READ) |
             NK_SECURE_OP_BIT(NK_SECURE_OP_LOAD), allow);
    add_rule(&policy, NK_SECURE_SUBJECT_UNTRUSTED, NK_SECURE_OBJECT_SYSTEM,
             NK_SECURE_OP_BIT(NK_SECURE_OP_READ), allow);
    add_rule(&policy, NK_SECURE_SUBJECT_UNTRUSTED, NK_SECURE_OBJECT_USER,
             NK_SECURE_OP_BIT(NK_SECURE_OP_READ) |
             NK_SECURE_OP_BIT(NK_SECURE_OP_EXEC), allow);
    add_rule(&policy, NK_SECURE_SUBJECT_UNTRUSTED, NK_SECURE_OBJECT_USB,
             NK_SECURE_OP_BIT(NK_SECURE_OP_READ) |
             NK_SECURE_OP_BIT(NK_SECURE_OP_LOAD), allow);

    *out = policy;
    return 0;
}

int policy_set(const nk_secure_policy_t *policy)
{
    if (policy == 0) {
        return 1;
    }
    if (g_policy_locked) {
        return 2;
    }
    if (policy->rule_count > (uint32_t)NK_SECURE_MAX_RULES ||
        policy->path_count > (uint32_t)NK_SECURE_MAX_PATH_RULES ||
        !decision_valid(policy->default_decision)) {
        return 1;
    }
    for (uint32_t i = 0u; i < policy->rule_count; i++) {
        const nk_secure_rule_t *rule = &policy->rules[i];

        if (!label_valid(&rule->subject) || !label_valid(&rule->object) ||
            !operation_valid(rule->operations) ||
            !decision_valid(rule->decision)) {
            return 1;
        }
    }
    for (uint32_t i = 0u; i < policy->path_count; i++) {
        const nk_secure_path_rule_t *path = &policy->paths[i];

        if (str_len(path->prefix) == 0u ||
            str_len(path->prefix) >= (uint32_t)NK_SECURE_PATH_MAX ||
            !label_valid(&path->object)) {
            return 1;
        }
    }
    install_policy(policy);
    return 0;
}

int policy_lock(void)
{
    g_policy_locked = 1;
    return 0;
}

int policy_is_locked(void)
{
    return g_policy_locked;
}

int label_path(const char *path, nk_secure_label_t *out)
{
    uint32_t best = 0u;
    uint32_t best_len = 0u;

    if (path == 0 || out == 0) {
        return 1;
    }
    ensure_policy();
    for (uint32_t i = 0u; i < g_policy.path_count; i++) {
        const nk_secure_path_rule_t *rule = &g_policy.paths[i];
        uint32_t len = str_len(rule->prefix);

        if (len > best_len && str_has_prefix(path, rule->prefix)) {
            best = i;
            best_len = len;
        }
    }
    if (best_len != 0u) {
        *out = g_policy.paths[best].object;
    } else {
        label_set(out, NK_SECURE_OBJECT_UNKNOWN);
    }
    return 0;
}

int decide_access(const char *subject, const char *path, uint32_t operation,
                  nk_secure_report_t *report)
{
    nk_secure_label_t object;
    char detail[NK_SECUREKERNEL_MAX_DETAIL];
    int allowed = 0;
    int denied = 0;

    if (report == 0) {
        return 1;
    }
    byte_zero(report, (uint32_t)sizeof(*report));
    report->decision = (uint32_t)NK_SECURE_DECISION_DENY;

    if (subject == 0 || path == 0 || operation >= (uint32_t)NK_SECURE_OP_COUNT) {
        report_detail(report, "invalid policy request");
        return 0;
    }
    ensure_policy();
    label_path(path, &object);

    for (uint32_t i = 0u; i < g_policy.rule_count; i++) {
        const nk_secure_rule_t *rule = &g_policy.rules[i];

        if (!str_equal(rule->subject.name, subject) ||
            !str_equal(rule->object.name, object.name) ||
            (rule->operations & (1u << operation)) == 0u) {
            continue;
        }
        if (rule->decision == (uint8_t)NK_SECURE_DECISION_DENY) {
            denied = 1;
        } else {
            allowed = 1;
        }
    }

    if (!denied && allowed) {
        report->decision = (uint32_t)NK_SECURE_DECISION_ALLOW;
    } else if (!denied && g_policy.default_decision ==
                              (uint8_t)NK_SECURE_DECISION_ALLOW) {
        report->decision = (uint32_t)NK_SECURE_DECISION_ALLOW;
    }
    if (report->decision == (uint32_t)NK_SECURE_DECISION_DENY) {
        report->flags |= (uint32_t)NK_SECURE_FLAG_POLICY_DENY;
    }
    policy_detail(detail, (uint32_t)sizeof(detail), subject, object.name,
                  operation, report->decision == (uint32_t)NK_SECURE_DECISION_ALLOW);
    report_detail(report, detail);
    return 0;
}

void audit_record(const char *subject, const char *object, uint32_t operation,
                  uint32_t decision, const char *reason)
{
    nk_secure_audit_t *entry = &g_audit[(g_audit_start + g_audit_count) %
                                         NK_SECURE_MAX_AUDIT];

    byte_zero(entry, (uint32_t)sizeof(*entry));
    entry->sequence = ++g_audit_sequence;
    str_copy(entry->subject, (uint32_t)sizeof(entry->subject), subject);
    str_copy(entry->object, (uint32_t)sizeof(entry->object), object);
    entry->operation = operation;
    entry->decision = decision;
    str_copy(entry->reason, (uint32_t)sizeof(entry->reason), reason);

    if (g_audit_count < (uint32_t)NK_SECURE_MAX_AUDIT) {
        g_audit_count++;
    } else {
        g_audit_start = (g_audit_start + 1u) % NK_SECURE_MAX_AUDIT;
    }
}

int access_report(const char *subject, const char *path, uint32_t operation,
                  nk_secure_report_t *report)
{
    int rc = decide_access(subject, path, operation, report);

    if (rc == 0) {
        nk_secure_label_t object;

        label_path(path, &object);
        audit_record(subject, object.name, operation, report->decision,
                     report->detail);
    }
    return rc;
}

int enforce_image(const char *subject, const char *path,
                  const uint8_t *image, uint32_t size,
                  nk_secure_report_t *report)
{
    nk_secure_report_t policy_report;
    nk_secure_label_t object;
    int rc;

    if (report == 0) {
        return 1;
    }
    rc = verify_image(image, size, report);
    if (rc != 0) {
        return rc;
    }
    rc = decide_access(subject, path, (uint32_t)NK_SECURE_OP_EXEC,
                       &policy_report);
    if (rc != 0) {
        return rc;
    }

    report->flags |= policy_report.flags;
    if (policy_report.decision == (uint32_t)NK_SECURE_DECISION_DENY) {
        report->decision = (uint32_t)NK_SECURE_DECISION_DENY;
        report_detail(report, policy_report.detail);
    }
    label_path(path, &object);
    audit_record(subject, object.name, (uint32_t)NK_SECURE_OP_EXEC,
                 report->decision, report->detail);
    return 0;
}

uint32_t audit_count(void)
{
    return g_audit_count;
}

int audit_last(nk_secure_audit_t *out)
{
    uint32_t index;

    if (out == 0 || g_audit_count == 0u) {
        return 1;
    }
    index = (g_audit_start + g_audit_count - 1u) % NK_SECURE_MAX_AUDIT;
    *out = g_audit[index];
    return 0;
}

uint32_t audit_drain(nk_secure_audit_t *out, uint32_t max)
{
    uint32_t n = max < g_audit_count ? max : g_audit_count;

    if (out == 0) {
        return 0u;
    }
    for (uint32_t i = 0u; i < n; i++) {
        out[i] = g_audit[(g_audit_start + i) % NK_SECURE_MAX_AUDIT];
    }
    g_audit_start = (g_audit_start + n) % NK_SECURE_MAX_AUDIT;
    g_audit_count -= n;
    return n;
}

uint32_t audit_format(const nk_secure_audit_t *entry, char *out, uint32_t cap)
{
    char detail[NK_SECUREKERNEL_MAX_DETAIL];

    if (entry == 0 || out == 0 || cap == 0u) {
        return 0u;
    }
    str_copy(detail, (uint32_t)sizeof(detail), "[securekernel] ");
    str_append(detail, (uint32_t)sizeof(detail),
               entry->decision == (uint32_t)NK_SECURE_DECISION_DENY
                   ? "DENY " : "ALLOW ");
    str_append(detail, (uint32_t)sizeof(detail), entry->subject);
    str_append(detail, (uint32_t)sizeof(detail), "/");
    str_append(detail, (uint32_t)sizeof(detail), entry->object);
    str_append(detail, (uint32_t)sizeof(detail), " op=");
    str_append(detail, (uint32_t)sizeof(detail), op_name(entry->operation));
    str_append(detail, (uint32_t)sizeof(detail), ": ");
    str_append(detail, (uint32_t)sizeof(detail), entry->reason);

    str_copy(out, cap, detail);
    return str_len(out);
}

} /* namespace nksecure */
