/*
 * bench_recv_buffer.c
 *
 * Benchmark for the verified CircularBuffer (Karamel-extracted from Pulse).
 * Two scenarios — sequential and out-of-order — across chunk sizes 2–64 B.
 * Reports write and read throughput (MB/s).
 *
 * Build:  make
 * Run:    ./bench_recv_buffer [iterations]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "../verified_wrapper_recv_buffer.h"

extern void krmlinit_globals(void);

/* ─── Timing helpers ──────────────────────────────────────────────── */

static inline uint64_t
now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline double
throughput_mbps(uint64_t total_bytes, uint64_t elapsed_ns)
{
    double secs = (double)elapsed_ns / 1e9;
    return secs > 0 ? ((double)total_bytes / (1024.0 * 1024.0)) / secs : 0;
}

/* ─── Simple PRNG (xorshift64) ────────────────────────────────────── */

static uint64_t rng_state = 0x123456789ABCDEF0ULL;

static inline uint64_t
xorshift64(void)
{
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static void
shuffle(uint32_t* arr, uint32_t n)
{
    for (uint32_t i = n - 1; i > 0; i--) {
        uint32_t j = (uint32_t)(xorshift64() % (i + 1));
        uint32_t tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

static void
fill_pattern(uint8_t* buf, uint32_t len, uint64_t offset)
{
    for (uint32_t i = 0; i < len; i++)
        buf[i] = (uint8_t)((offset + i) & 0xFF);
}

/* ─── Result for one (scenario, chunk_size) point ─────────────────── */

typedef struct {
    uint32_t chunk_size;
    double   write_mbps;
    double   read_mbps;
} point_t;

/* ─── Sequential writes + reads ───────────────────────────────────── */

static point_t
bench_sequential(uint32_t iterations, uint32_t chunk_size)
{
    point_t r = { .chunk_size = chunk_size };
    uint8_t* data = malloc(chunk_size);
    uint32_t alloc_len = 65536;
    uint32_t n_chunks = alloc_len / chunk_size;
    uint64_t total_bytes = (uint64_t)n_chunks * chunk_size * iterations;
    uint64_t write_ns = 0, read_ns = 0;

    for (uint32_t iter = 0; iter < iterations; iter++) {
        VERIFIED_RECV_BUFFER buf = {0};
        VerifiedRecvBufferInitialize(&buf, alloc_len, alloc_len);
        BOOLEAN ndr;

        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < n_chunks; i++) {
            fill_pattern(data, chunk_size, (uint64_t)i * chunk_size);
            VerifiedRecvBufferWrite(&buf, (uint64_t)i * chunk_size,
                                    (uint16_t)chunk_size, data, &ndr);
        }
        uint64_t t1 = now_ns();

        uint64_t offset;
        uint32_t count = 2;
        QUIC_BUFFER buffers[2] = {0};
        VerifiedRecvBufferRead(&buf, &offset, &count, buffers);
        uint64_t total = 0;
        for (uint32_t i = 0; i < count; i++) total += buffers[i].Length;
        VerifiedRecvBufferDrain(&buf, total);
        uint64_t t2 = now_ns();

        write_ns += (t1 - t0);
        read_ns  += (t2 - t1);

        VerifiedRecvBufferUninitialize(&buf);
    }

    r.write_mbps = throughput_mbps(total_bytes, write_ns);
    r.read_mbps  = throughput_mbps(total_bytes, read_ns);
    free(data);
    return r;
}

/* ─── Out-of-order writes + reads ─────────────────────────────────── */

static point_t
bench_ooo(uint32_t iterations, uint32_t chunk_size)
{
    point_t r = { .chunk_size = chunk_size };
    uint8_t* data = malloc(chunk_size);
    uint32_t alloc_len = 65536;
    uint32_t n_chunks = alloc_len / chunk_size;
    uint64_t total_bytes = (uint64_t)n_chunks * chunk_size * iterations;
    uint64_t write_ns = 0, read_ns = 0;

    uint32_t* order = malloc(n_chunks * sizeof(uint32_t));
    for (uint32_t i = 0; i < n_chunks; i++) order[i] = i;

    for (uint32_t iter = 0; iter < iterations; iter++) {
        VERIFIED_RECV_BUFFER buf = {0};
        VerifiedRecvBufferInitialize(&buf, alloc_len, alloc_len);
        shuffle(order, n_chunks);
        BOOLEAN ndr;

        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < n_chunks; i++) {
            uint64_t off = (uint64_t)order[i] * chunk_size;
            fill_pattern(data, chunk_size, off);
            VerifiedRecvBufferWrite(&buf, off, (uint16_t)chunk_size,
                                    data, &ndr);
        }
        uint64_t t1 = now_ns();

        uint64_t offset;
        uint32_t count = 2;
        QUIC_BUFFER buffers[2] = {0};
        VerifiedRecvBufferRead(&buf, &offset, &count, buffers);
        uint64_t total = 0;
        for (uint32_t i = 0; i < count; i++) total += buffers[i].Length;
        VerifiedRecvBufferDrain(&buf, total);
        uint64_t t2 = now_ns();

        write_ns += (t1 - t0);
        read_ns  += (t2 - t1);

        VerifiedRecvBufferUninitialize(&buf);
    }

    r.write_mbps = throughput_mbps(total_bytes, write_ns);
    r.read_mbps  = throughput_mbps(total_bytes, read_ns);
    free(order);
    free(data);
    return r;
}

/* ─── Main ────────────────────────────────────────────────────────── */

static const uint32_t SIZES[] = { 2, 4, 8, 16, 32, 64 };
#define N_SIZES (sizeof(SIZES) / sizeof(SIZES[0]))

int main(int argc, char* argv[])
{
    uint32_t iterations = 200;
    const char* gnuplot_file = NULL;
    const char* label = "verified";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gnuplot") == 0 && i + 1 < argc)
            gnuplot_file = argv[++i];
        else if (strcmp(argv[i], "--label") == 0 && i + 1 < argc)
            label = argv[++i];
        else {
            uint32_t v = (uint32_t)atoi(argv[i]);
            if (v > 0) iterations = v;
        }
    }

    krmlinit_globals();

    point_t seq[N_SIZES], ooo[N_SIZES];

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  CircularBuffer Benchmark  (%s, %u iterations)\n",
           label, iterations);
    printf("═══════════════════════════════════════════════════════════════\n\n");

    /* Sequential */
    printf("  Sequential writes + reads\n");
    printf("  %-10s %12s %12s\n", "ChunkSize", "Write MB/s", "Read MB/s");
    printf("  ──────────────────────────────────────\n");
    for (uint32_t s = 0; s < N_SIZES; s++) {
        seq[s] = bench_sequential(iterations, SIZES[s]);
        printf("  %-10u %12.2f %12.2f\n",
               seq[s].chunk_size, seq[s].write_mbps, seq[s].read_mbps);
    }

    printf("\n");

    /* Out-of-order */
    printf("  Out-of-order writes + reads\n");
    printf("  %-10s %12s %12s\n", "ChunkSize", "Write MB/s", "Read MB/s");
    printf("  ──────────────────────────────────────\n");
    for (uint32_t s = 0; s < N_SIZES; s++) {
        ooo[s] = bench_ooo(iterations, SIZES[s]);
        printf("  %-10u %12.2f %12.2f\n",
               ooo[s].chunk_size, ooo[s].write_mbps, ooo[s].read_mbps);
    }

    printf("\n═══════════════════════════════════════════════════════════════\n");

    /* ─── Gnuplot output ──────────────────────────────────────────── */
    if (gnuplot_file) {
        /*
         * Data blocks (separated by double blank lines for gnuplot "index"):
         *   index 0: sequential   (columns: chunk_size  write_mbps  read_mbps)
         *   index 1: out-of-order
         *
         * Run twice (verified, unverified) → 4 blocks total:
         *   index 0: verified sequential
         *   index 1: verified ooo
         *   index 2: unverified sequential
         *   index 3: unverified ooo
         */
        int append = 0;
        FILE* check = fopen(gnuplot_file, "r");
        if (check) { append = (fgetc(check) != EOF); fclose(check); }

        FILE* fp = fopen(gnuplot_file, append ? "a" : "w");
        if (fp) {
            if (!append) {
                fprintf(fp, "# Columns: ChunkSize  WriteMBps  ReadMBps\n");
                fprintf(fp, "# index 0,2: sequential   index 1,3: ooo\n\n");
            } else {
                fprintf(fp, "\n\n");
            }

            /* Sequential block */
            fprintf(fp, "# %s sequential\n", label);
            for (uint32_t s = 0; s < N_SIZES; s++)
                fprintf(fp, "%u\t%.2f\t%.2f\n",
                        seq[s].chunk_size, seq[s].write_mbps, seq[s].read_mbps);

            /* OOO block */
            fprintf(fp, "\n\n# %s ooo\n", label);
            for (uint32_t s = 0; s < N_SIZES; s++)
                fprintf(fp, "%u\t%.2f\t%.2f\n",
                        ooo[s].chunk_size, ooo[s].write_mbps, ooo[s].read_mbps);

            fclose(fp);
            printf("Gnuplot data → %s (%s)\n", gnuplot_file, label);
        }
    }

    return 0;
}
