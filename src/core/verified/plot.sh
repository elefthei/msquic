#!/bin/bash
# plot.sh — Run verified & unverified benchmarks and generate gnuplot charts.
#
# Usage:
#   ./plot.sh [iterations]          # default: 200
#   ./plot.sh 500                   # more iterations for stable results
#
# Generates two figures:
#   sequential.png — Sequential write/read throughput vs chunk size
#   ooo.png        — Out-of-order write/read throughput vs chunk size

set -e
cd "$(dirname "$0")"

ITERS="${1:-200}"
DAT="bench.dat"

rm -f "$DAT"

echo "=== Running verified benchmark ($ITERS iterations) ==="
./bench_recv_buffer "$ITERS" --gnuplot "$DAT" --label verified

if [ -x ./bench_recv_buffer_orig ]; then
    echo ""
    echo "=== Running unverified benchmark ($ITERS iterations) ==="
    ./bench_recv_buffer_orig "$ITERS" --gnuplot "$DAT" --label unverified
else
    echo ""
    echo "WARNING: bench_recv_buffer_orig not found, running verified again as placeholder"
    ./bench_recv_buffer "$ITERS" --gnuplot "$DAT" --label unverified
fi

echo ""
echo "=== Generating plots ==="

# Data layout in bench.dat:
#   index 0: verified sequential   (cols: chunk_size  write_mbps  read_mbps)
#   index 1: verified ooo
#   index 2: unverified sequential
#   index 3: unverified ooo

gnuplot <<'GNUPLOT'
set terminal pngcairo size 900,500 enhanced font 'Arial,11'
set style data linespoints
set grid ytics
set key top left
set yrange [0:*]
set xlabel 'Chunk Size (bytes)'
set logscale x 2
set xrange [1:128]

set output 'sequential.png'
set title 'Sequential Throughput — Verified vs Unverified'
set ylabel 'Throughput (MB/s)'
plot 'bench.dat' index 0 using 1:2 title 'verified write'   lw 2 pt 7 ps 1.2, \
     'bench.dat' index 0 using 1:3 title 'verified read'    lw 2 pt 9 ps 1.2, \
     'bench.dat' index 2 using 1:2 title 'unverified write' lw 2 pt 5 ps 1.2 dt 2, \
     'bench.dat' index 2 using 1:3 title 'unverified read'  lw 2 pt 11 ps 1.2 dt 2

set output 'ooo.png'
set title 'Out-of-Order Throughput — Verified vs Unverified'
set ylabel 'Throughput (MB/s)'
plot 'bench.dat' index 1 using 1:2 title 'verified write'   lw 2 pt 7 ps 1.2, \
     'bench.dat' index 1 using 1:3 title 'verified read'    lw 2 pt 9 ps 1.2, \
     'bench.dat' index 3 using 1:2 title 'unverified write' lw 2 pt 5 ps 1.2 dt 2, \
     'bench.dat' index 3 using 1:3 title 'unverified read'  lw 2 pt 11 ps 1.2 dt 2
GNUPLOT

echo "Generated: sequential.png  ooo.png"
