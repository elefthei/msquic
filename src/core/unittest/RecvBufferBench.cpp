/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

Abstract:

    Benchmark for QUIC_RECV_BUFFER in CIRCULAR vs SINGLE mode.
    Usage: recvbufferbench [iterations]
    Time with: time ./recvbufferbench

--*/

#include "precomp.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

static const uint32_t BufferSizes[] = { 64, 256, 1024, 4096, 16384, 65536 };
static const int NumSizes = sizeof(BufferSizes) / sizeof(BufferSizes[0]);

struct BenchRecvBuffer {
    QUIC_RECV_BUFFER Buf;

    bool Init(QUIC_RECV_BUF_MODE Mode, uint32_t AllocLen, uint32_t VirtualLen) {
        memset(&Buf, 0, sizeof(Buf));
        return QUIC_SUCCEEDED(
            QuicRecvBufferInitialize(&Buf, AllocLen, VirtualLen, Mode, NULL));
    }

    void Uninit() {
        if (Buf.ReadPendingLength != 0) {
            QuicRecvBufferDrain(&Buf, Buf.ReadPendingLength);
        }
        QuicRecvBufferUninitialize(&Buf);
    }

    bool Write(uint64_t Offset, uint16_t Len, const uint8_t* Data) {
        uint64_t Quota = UINT64_MAX;
        uint64_t QuotaConsumed = 0;
        BOOLEAN Ready = FALSE;
        uint64_t SizeNeeded = 0;
        return QUIC_SUCCEEDED(
            QuicRecvBufferWrite(
                &Buf, Offset, Len, Data,
                Quota, &QuotaConsumed, &Ready, &SizeNeeded));
    }

    uint32_t Read(QUIC_BUFFER* Buffers, uint32_t MaxCount) {
        uint64_t Offset = 0;
        uint32_t Count = MaxCount;
        QuicRecvBufferRead(&Buf, &Offset, &Count, Buffers);
        return Count;
    }

    bool Drain(uint64_t Len) {
        return QuicRecvBufferDrain(&Buf, Len) != FALSE;
    }
};

//
// Benchmark: sequential write + read + drain cycles.
// Simulates a stream receiving data in order.
//
static double
BenchWriteReadDrain(
    QUIC_RECV_BUF_MODE Mode,
    uint32_t BufSize,
    int Iterations
    )
{
    BenchRecvBuffer Rb;
    if (!Rb.Init(Mode, BufSize, BufSize)) {
        fprintf(stderr, "Failed to init buffer size=%u mode=%u\n", BufSize, Mode);
        return -1.0;
    }

    //
    // Prepare write data (16-byte chunks to simulate small TLS records).
    //
    const uint16_t WriteChunk = 16;
    uint8_t WriteData[16];
    memset(WriteData, 0xAB, sizeof(WriteData));

    QUIC_BUFFER ReadBufs[3];

    auto Start = std::chrono::high_resolution_clock::now();

    uint64_t StreamOffset = 0;
    for (int iter = 0; iter < Iterations; iter++) {
        //
        // Fill the buffer to ~75% capacity in WriteChunk increments.
        //
        uint32_t BytesWritten = 0;
        uint32_t Target = (BufSize * 3) / 4;
        while (BytesWritten + WriteChunk <= Target) {
            if (!Rb.Write(StreamOffset, WriteChunk, WriteData)) break;
            StreamOffset += WriteChunk;
            BytesWritten += WriteChunk;
        }

        //
        // Read all available data.
        //
        uint32_t Count = 3;
        Rb.Read(ReadBufs, Count);

        //
        // Drain everything we wrote.
        //
        Rb.Drain(BytesWritten);
    }

    auto End = std::chrono::high_resolution_clock::now();
    double Ms = std::chrono::duration<double, std::milli>(End - Start).count();

    Rb.Uninit();
    return Ms;
}

//
// Benchmark: bulk write filling the entire buffer, then resize (grow 2x).
//
static double
BenchResize(
    QUIC_RECV_BUF_MODE Mode,
    uint32_t BufSize,
    int Iterations
    )
{
    const uint16_t WriteChunk = 64;
    uint8_t WriteData[64];
    memset(WriteData, 0xCD, sizeof(WriteData));

    auto Start = std::chrono::high_resolution_clock::now();

    for (int iter = 0; iter < Iterations; iter++) {
        BenchRecvBuffer Rb;
        if (!Rb.Init(Mode, BufSize, BufSize)) {
            return -1.0;
        }

        //
        // Fill buffer to ~50%.
        //
        uint64_t Off = 0;
        uint32_t Target = BufSize / 2;
        uint32_t Written = 0;
        while (Written + WriteChunk <= Target) {
            Rb.Write(Off, WriteChunk, WriteData);
            Off += WriteChunk;
            Written += WriteChunk;
        }

        //
        // Drain ~25% to create a non-trivial ReadStart for circular mode.
        //
        uint32_t DrainAmt = Written / 2;
        Rb.Drain(DrainAmt);

        //
        // Write more data to shift ReadStart further.
        //
        uint32_t MoreWrite = DrainAmt;
        uint32_t Wrote2 = 0;
        while (Wrote2 + WriteChunk <= MoreWrite) {
            Rb.Write(Off, WriteChunk, WriteData);
            Off += WriteChunk;
            Wrote2 += WriteChunk;
        }

        //
        // Trigger resize by increasing virtual buffer length and writing past capacity.
        // For SINGLE/CIRCULAR, QuicRecvBufferWrite calls resize internally.
        //
        QuicRecvBufferIncreaseVirtualBufferLength(&Rb.Buf, BufSize * 2);
        uint32_t FillTarget = BufSize; // write up to new capacity
        uint32_t Wrote3 = 0;
        while (Wrote3 + WriteChunk <= FillTarget) {
            Rb.Write(Off, WriteChunk, WriteData);
            Off += WriteChunk;
            Wrote3 += WriteChunk;
        }

        //
        // Read + drain all.
        //
        QUIC_BUFFER ReadBufs[3];
        uint32_t Count = 3;
        Rb.Read(ReadBufs, Count);
        uint32_t TotalRead = 0;
        for (uint32_t i = 0; i < Count; i++) TotalRead += ReadBufs[i].Length;
        Rb.Drain(TotalRead);

        Rb.Uninit();
    }

    auto End = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(End - Start).count();
}

static const char*
ModeName(QUIC_RECV_BUF_MODE Mode)
{
    switch (Mode) {
    case QUIC_RECV_BUF_MODE_SINGLE:   return "SINGLE  ";
    case QUIC_RECV_BUF_MODE_CIRCULAR: return "CIRCULAR";
    case QUIC_RECV_BUF_MODE_MULTIPLE: return "MULTIPLE";
    default:                          return "UNKNOWN ";
    }
}

int main(int argc, char* argv[])
{
    int Iterations = 1000;
    if (argc > 1) {
        Iterations = atoi(argv[1]);
        if (Iterations <= 0) Iterations = 1000;
    }

    CxPlatSystemLoad();
    CxPlatInitialize();

    printf("RecvBuffer Benchmark — %d iterations per (mode, size) pair\n", Iterations);
    printf("============================================================\n\n");

    //
    // Write/Read/Drain benchmark.
    //
    printf("--- Write/Read/Drain (75%% fill per iteration) ---\n");
    printf("%-10s %8s %10s %12s\n", "Mode", "BufSize", "Time(ms)", "Ops/sec");
    printf("%-10s %8s %10s %12s\n", "--------", "-------", "--------", "----------");

    QUIC_RECV_BUF_MODE Modes[] = {
        QUIC_RECV_BUF_MODE_SINGLE,
        QUIC_RECV_BUF_MODE_CIRCULAR
    };

    for (auto Mode : Modes) {
        for (int s = 0; s < NumSizes; s++) {
            double Ms = BenchWriteReadDrain(Mode, BufferSizes[s], Iterations);
            double OpsPerSec = (Ms > 0) ? (Iterations / (Ms / 1000.0)) : 0;
            printf("%-10s %8u %10.2f %12.0f\n",
                   ModeName(Mode), BufferSizes[s], Ms, OpsPerSec);
        }
    }

    //
    // Resize benchmark.
    //
    int ResizeIters = Iterations / 10;
    if (ResizeIters < 10) ResizeIters = 10;

    printf("\n--- Resize (grow 2x, %d iterations) ---\n", ResizeIters);
    printf("%-10s %8s %10s %12s\n", "Mode", "BufSize", "Time(ms)", "Ops/sec");
    printf("%-10s %8s %10s %12s\n", "--------", "-------", "--------", "----------");

    for (auto Mode : Modes) {
        for (int s = 0; s < NumSizes; s++) {
            double Ms = BenchResize(Mode, BufferSizes[s], ResizeIters);
            double OpsPerSec = (Ms > 0) ? (ResizeIters / (Ms / 1000.0)) : 0;
            printf("%-10s %8u %10.2f %12.0f\n",
                   ModeName(Mode), BufferSizes[s], Ms, OpsPerSec);
        }
    }

    printf("\nDone.\n");

    CxPlatUninitialize();
    CxPlatSystemUnload();

    return 0;
}
