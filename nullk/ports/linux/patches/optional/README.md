# Optional Linux patch set (not yet applied)

Phase 1 is entirely out-of-tree. The `nkabi3` LKM already creates the ABI v3
control plane and exports:

```c
u32 nk64_parasite_mode(void);
```

A later patch against Linux 6.6.162 LTS can call this from selected kernel
sites without linking against module internals:

- scheduler: honor `NK64_PARASITE_COOPERATIVE` hints from managed tasks;
- exec: consult SecureKernel policy before replacing a managed task image;
- VFS: map `@/` paths for managed tasks before generic Linux path lookup;
- cgroup/CPU: reserve host CPUs and pin managed tasks when in `MANAGED` mode.

Keep every patch additive and behind the exported mode check so an unpatched or
module-less kernel keeps normal Linux behavior.
