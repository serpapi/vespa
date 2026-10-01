Supporting material for REPORT.md. Not meant for upstream.

fusion_profile.cpp   builds disk indexes from Common Crawl WET files and fuses them (Task 2). Built on the
                     benchmark box by appending it to searchlib/src/tests/diskindex/fusion/CMakeLists.txt as
                     searchlib_fusion_profile_app (DEPENDS vespa_searchlib searchlib_test).
madv_hot_test.c      hot-set eviction test for MADV_SEQUENTIAL vs MADV_NORMAL (Task 3), run in a 2 GiB
                     MemoryMax cgroup via systemd-run.
scripts/             what ran on the EC2 box (paths under /work, build container "vbuild", perf container
                     "vperf"). strict-blender-v1-intermediate.patch is the first version of the change (v1 in
                     REPORT.md), kept for the v1/v2 comparison.
results/sourceblender  benchmark CSV output (bench-*.txt), perf stat output per configuration and
                       implementation (perfstat-v1/, perfstat-v2/), ctest output.
results/fusion       fusion logs, perf stat, perf report/annotate excerpts, ablation variants, scaling runs.
results/both-bases   second box: build/ctest/benchmark output for agents/serpapi-vespa-perf (integ) and
                     agents/serpapi-8.679.50 (prod), built from a git bundle of exactly those commits.
scripts/both-bases/  the pipeline for that box (setup, targets.py, build, test, bench-both).
results/madvise      result4-* = final test shape (hot set touched 16x per round); result3-* = first shape
                     (touched once per round), which cannot show the difference (see REPORT.md).
