// Does the kernel protect hot file pages that are only accessed through an mmap with
// MADV_SEQUENTIAL? Hot set = first 256 MiB of the file. Each round streams 256 MiB of cold
// pages in 16 slices and touches the whole hot set after every slice (hot pages are used
// far more often than cold ones, as hot posting lists are). Run inside a memory-limited
// cgroup and compare advice.
//
// usage: madv_hot_test <file> <normal|sequential|random> <rounds>
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

static long majflt(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_majflt;
}

// active_file/inactive_file/workingset_activate_file of our memory cgroup (cgroup v2)
static void memcg_stat(long* active, long* inactive, long* activate) {
    char  path[512] = "/sys/fs/cgroup";
    FILE* f = fopen("/proc/self/cgroup", "r");
    char  line[512];
    if (f != NULL && fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\n")] = 0;
        snprintf(path, sizeof(path), "/sys/fs/cgroup%s/memory.stat", strchr(line, ':') + 2);
    }
    if (f != NULL) {
        fclose(f);
    }
    *active = *inactive = *activate = -1;
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    char key[128];
    long value;
    while (fscanf(f, "%127s %ld", key, &value) == 2) {
        if (strcmp(key, "active_file") == 0) {
            *active = value >> 20;
        } else if (strcmp(key, "inactive_file") == 0) {
            *inactive = value >> 20;
        } else if (strcmp(key, "workingset_activate_file") == 0) {
            *activate = value;
        }
    }
    fclose(f);
}

static double resident_fraction(const uint8_t* base, size_t len, size_t page) {
    size_t         pages = len / page;
    unsigned char* vec = malloc(pages);
    if (mincore((void*)base, len, vec) != 0) {
        perror("mincore");
        exit(1);
    }
    size_t resident = 0;
    for (size_t i = 0; i < pages; ++i) {
        resident += vec[i] & 1;
    }
    free(vec);
    return (double)resident / (double)pages;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <file> <normal|sequential|random> <rounds>\n", argv[0]);
        return 1;
    }
    int advice = MADV_NORMAL;
    if (strcmp(argv[2], "sequential") == 0) {
        advice = MADV_SEQUENTIAL;
    } else if (strcmp(argv[2], "random") == 0) {
        advice = MADV_RANDOM;
    }
    int         rounds = atoi(argv[3]);
    int         fd = open(argv[1], O_RDONLY);
    struct stat st;
    fstat(fd, &st);
    size_t   len = st.st_size;
    size_t   page = sysconf(_SC_PAGESIZE);
    uint8_t* base = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED || madvise(base, len, advice) != 0) {
        perror("mmap/madvise");
        return 1;
    }
    const size_t hot = 256ul << 20;
    const size_t chunk = 256ul << 20;
    size_t       cold_pos = hot;
    uint64_t     sum = 0;
    long         hot_faults_total = 0;
    double       residency_total = 0.0;
    for (int r = 0; r < rounds; ++r) {
        double resident = resident_fraction(base, hot, page);
        long   hot_faults = 0;
        for (size_t slice = 0; slice < 16; ++slice) {
            long before = majflt();
            for (size_t off = 0; off < hot; off += page) {
                sum += base[off];
            }
            hot_faults += majflt() - before;
            for (size_t off = slice * (chunk / 16); off < (slice + 1) * (chunk / 16); off += page) {
                sum += base[cold_pos + off];
            }
        }
        cold_pos += chunk;
        if (cold_pos + chunk > len) {
            cold_pos = hot;
        }
        if (r >= 4) { // after warm-up
            hot_faults_total += hot_faults;
            residency_total += resident;
        }
        long active, inactive, activate;
        memcg_stat(&active, &inactive, &activate);
        printf("round %2d hot_resident_before=%5.1f%% hot_major_faults=%ld active_file=%ldM inactive_file=%ldM "
               "workingset_activate_file=%ld\n",
               r, 100.0 * resident, hot_faults, active, inactive, activate);
    }
    int measured = rounds - 4;
    printf("advice=%s rounds=%d avg_hot_resident_before_touch=%.1f%% avg_hot_major_faults_per_round=%.0f "
           "(hot set %zu pages) sum=%lu\n",
           argv[2], measured, 100.0 * residency_total / measured, (double)hot_faults_total / measured, hot / page,
           (unsigned long)sum);
    return 0;
}
