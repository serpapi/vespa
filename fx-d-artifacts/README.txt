Supporting material for FX-D-REPORT.md. Not meant for upstream.

results/
  bench-20261017T103602Z.txt      main run: 10 rounds of A B C0 C, clone + ctor, plus A grow; 590 M lids
  bench-20261017T103501Z.txt      single smoke round before the main run
  bench-thp-never-*.txt           same, THP=never, 5 rounds
  bench-memcpy-*.txt              memcpy into a fresh Vespa Alloc vs C clone, 10 rounds
  kernel-only.txt                 pre-faulted kernel timings: memcpy, shipped kernel, no-max, blocked4 (scalar)
  kernel-only-2.txt               same with lanes64 instead of blocked4
  dump-small-*.txt, d-small-*.cmp cross-build digests, 549 cases per variant; l-*.cmp = log messages minus timestamp/pid
  dump-big-*.txt, d-big-*.cmp     4 cases at 590 M lids per variant
  dump3-small-*.txt, d3-*.cmp     re-run after the first review fix (A, B, C; its read check was wrong at first, see report)
  dump4-*.txt, d4-*.cmp, l4-*     final run of every dump (small + 590 M) with the final program, A B C0 C,
                                  incl. committedAfterCommit (uncommitted-limit probe)
  forced-persistence-failure.txt  dump in a 512 KiB tmpfs: ENOSPC aborts with exit 134
  sha256-bench-binaries.txt       benchmark program binaries per variant and revision
  ctest-*.txt                     9 affected test binaries per variant
  failing-first-on-A.txt          B's new tests (minus the init_docs API test) against the base library
  sha256-*.txt                    variant library and binary hashes
  make-*.log, cmake.log           build logs
  host-load.txt                   macOS load average around the runs (shared podman VM was busy)
disasm/official-8.679.50/         objdump of vespa-8.679.50-1.el8.aarch64 libvespa_searchlib.so / libvespalib.so
disasm/local/                     objdump of the local C0 (scalar) and C (vectorised) builds
variants/                         patches to rebuild every measured library and benchmark revision; MANIFEST.txt
scripts/                          setup.sh (chroot), inroot.sh, variant.sh, sync.sh, test-and-save.sh,
                                  failing-first.sh, bench.sh, stats.py, kern.cpp

Variants: A = 8ec58e1230 + bench program; B = A + ddd975bf17; C0 = B + first (scalar) version of the remap;
C = B + 9375939a23. Built in a chroot of vespaengine/vespa-build-almalinux-8@sha256:d705ce8b5027... in a lima vz VM.
