#!/bin/bash
# plot.sh — Generate gnuplot charts from bench.dat.
#
# Usage:
#   ./plot.sh [--png|--pdf]
#
# Requires bench.dat (produced by: make bench).
#
# --png (default): sequential.png + ooo.png   (pngcairo)
# --pdf:           sequential.tex + ooo.tex   (cairolatex pdf, for LaTeX)

set -e
cd "$(dirname "$0")"

FMT="png"
for arg in "$@"; do
    case "$arg" in
        --pdf) FMT="pdf" ;;
        --png) FMT="png" ;;
    esac
done

DAT="bench.dat"
if [ ! -f "$DAT" ]; then
    echo "Error: $DAT not found. Run 'make bench' first." >&2
    exit 1
fi

# Data layout in bench.dat:
#   index 0: verified sequential   (cols: chunk_size  write_mbps  read_mbps)
#   index 1: verified ooo
#   index 2: unverified sequential
#   index 3: unverified ooo

if [ "$FMT" = "pdf" ]; then
    TERM="set terminal cairolatex pdf size 4.5in,3in font ',10'"
    EXT="tex"
else
    TERM="set terminal pngcairo size 900,500 enhanced font 'Arial,11'"
    EXT="png"
fi

gnuplot <<GNUPLOT
$TERM
set style data linespoints
set grid ytics
set key top left
set yrange [0:*]
set xlabel 'Chunk Size (bytes)'
set logscale x 2
set xrange [1:128]

set output 'seq_write.$EXT'
set title 'Sequential Write Throughput'
set ylabel 'Throughput (MB/s)'
plot 'bench.dat' index 0 using 1:2 title 'verified'   lw 2 pt 7 ps 1.2, \
     'bench.dat' index 2 using 1:2 title 'unverified' lw 2 pt 5 ps 1.2 dt 2

set output 'seq_read.$EXT'
set title 'Sequential Read Throughput'
set ylabel 'Throughput (MB/s)'
plot 'bench.dat' index 0 using 1:3 title 'verified'   lw 2 pt 7 ps 1.2, \
     'bench.dat' index 2 using 1:3 title 'unverified' lw 2 pt 5 ps 1.2 dt 2

set output 'ooo_write.$EXT'
set title 'Out-of-Order Write Throughput'
set ylabel 'Throughput (MB/s)'
plot 'bench.dat' index 1 using 1:2 title 'verified'   lw 2 pt 7 ps 1.2, \
     'bench.dat' index 3 using 1:2 title 'unverified' lw 2 pt 5 ps 1.2 dt 2

set output 'ooo_read.$EXT'
set title 'Out-of-Order Read Throughput'
set ylabel 'Throughput (MB/s)'
plot 'bench.dat' index 1 using 1:3 title 'verified'   lw 2 pt 7 ps 1.2, \
     'bench.dat' index 3 using 1:3 title 'unverified' lw 2 pt 5 ps 1.2 dt 2
GNUPLOT

echo "Generated: seq_write.$EXT  seq_read.$EXT  ooo_write.$EXT  ooo_read.$EXT"
