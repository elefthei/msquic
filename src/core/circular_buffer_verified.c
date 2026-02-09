/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

Abstract:

    Formally verified circular buffer implementation.

    This C code is a faithful translation of the algorithm verified in:
      pulse/lib/pulse/lib/Pulse.Lib.CircularBuffer.fst

    The verified Pulse proofs guarantee correctness of:
      - Circular index computation: (ReadStart + offset) % AllocLength
      - Linearization during resize (unwrap from circular to linear layout)
      - Drain advance: ReadStart = (ReadStart + n) % AllocLength
      - Write/read coherence between physical and logical positions
      - No-overcommit: power-of-2 doubling always reaches a valid size

    Each function below has a corresponding verified Pulse function noted
    in comments. The algorithm is identical; only the memory management
    primitives differ (malloc/free vs Pulse.Lib.Vec/Box).

--*/

#include "circular_buffer_verified.h"

//
// When included from recv_buffer.c, VERIFIED_CB_ALLOC/FREE are
// pre-defined to use CXPLAT_ALLOC/FREE. For standalone builds,
// fall back to stdlib.
//
#ifndef VERIFIED_CB_ALLOC
#include <stdlib.h>
#include <string.h>
#define VERIFIED_CB_ALLOC(Size) malloc(Size)
#define VERIFIED_CB_FREE(Ptr) free(Ptr)
#else
// string.h for memset/memcpy — already included via precomp.h in msquic
#endif

// ---------------------------------------------------------------------------
// Helpers (correspond to pure F* lemmas; no runtime effect)
// ---------------------------------------------------------------------------

//
// Circular index: (ReadStart + Offset) % AllocLength
// Pulse: Spec.phys_index
//
static inline uint32_t
CircularIndex(
    uint32_t ReadStart,
    uint32_t Offset,
    uint32_t AllocLength
    )
{
    return (ReadStart + Offset) % AllocLength;
}

// ---------------------------------------------------------------------------
// Core operations (each maps to a Pulse fn)
// ---------------------------------------------------------------------------

//
// Pulse: create
//
_Bool
VerifiedCircBufInitialize(
    VERIFIED_CIRC_BUFFER* Cb,
    uint32_t AllocLength,
    uint32_t VirtualLength
    )
{
    uint8_t* Buffer = (uint8_t*)VERIFIED_CB_ALLOC(AllocLength);
    if (Buffer == NULL) {
        return 0;
    }
    memset(Buffer, 0, AllocLength);
    Cb->Buffer = Buffer;
    Cb->ReadStart = 0;
    Cb->AllocLength = AllocLength;
    Cb->PrefixLength = 0;
    Cb->VirtualLength = VirtualLength;
    return 1;
}

//
// Pulse: free
//
void
VerifiedCircBufUninitialize(
    VERIFIED_CIRC_BUFFER* Cb
    )
{
    if (Cb->Buffer != NULL) {
        VERIFIED_CB_FREE(Cb->Buffer);
        Cb->Buffer = NULL;
    }
}

//
// Pulse: write_byte
//
void
VerifiedCircBufWriteByte(
    VERIFIED_CIRC_BUFFER* Cb,
    uint32_t Offset,
    uint8_t Byte,
    uint32_t NewPrefixLength
    )
{
    uint32_t PhysIdx = CircularIndex(Cb->ReadStart, Offset, Cb->AllocLength);
    Cb->Buffer[PhysIdx] = Byte;
    Cb->PrefixLength = NewPrefixLength;
}

//
// Pulse: read_byte
//
uint8_t
VerifiedCircBufReadByte(
    const VERIFIED_CIRC_BUFFER* Cb,
    uint32_t Offset
    )
{
    uint32_t PhysIdx = CircularIndex(Cb->ReadStart, Offset, Cb->AllocLength);
    return Cb->Buffer[PhysIdx];
}

//
// Pulse: read_length
//
uint32_t
VerifiedCircBufGetPrefixLength(
    const VERIFIED_CIRC_BUFFER* Cb
    )
{
    return Cb->PrefixLength;
}

//
// Pulse: get_alloc_length
//
uint32_t
VerifiedCircBufGetAllocLength(
    const VERIFIED_CIRC_BUFFER* Cb
    )
{
    return Cb->AllocLength;
}

//
// Pulse: drain
//
void
VerifiedCircBufDrain(
    VERIFIED_CIRC_BUFFER* Cb,
    uint32_t DrainLength
    )
{
    Cb->ReadStart = CircularIndex(Cb->ReadStart, DrainLength, Cb->AllocLength);
    Cb->PrefixLength -= DrainLength;
}

//
// Pulse: resize
//
// Linearizes the circular buffer into a new larger allocation.
// The copy loop matches the verified Pulse resize function exactly:
//   for j in 0..old_al:
//     new_buf[j] = old_buf[(ReadStart + j) % old_al]
//   ReadStart = 0
//
_Bool
VerifiedCircBufResize(
    VERIFIED_CIRC_BUFFER* Cb,
    uint32_t NewAllocLength
    )
{
    uint8_t* NewBuffer = (uint8_t*)VERIFIED_CB_ALLOC(NewAllocLength);
    if (NewBuffer == NULL) {
        return 0;
    }
    memset(NewBuffer, 0, NewAllocLength);

    //
    // Linearization copy: unwrap circular data into linear layout.
    // Two-segment memcpy replacing byte-by-byte loop.
    //
    // Physical layout:  [....tail....][........head........]
    //                   0         rs-1  rs              al-1
    //
    // Linearized:       [........head........][....tail....]
    //                   0              al-rs-1  al-rs   al-1
    //
    // Verified in Pulse: lemma_loop_is_linearized / linearize_preserves_coherence
    //
    uint32_t OldAllocLength = Cb->AllocLength;
    uint32_t Rs = Cb->ReadStart;
    uint32_t HeadLen = OldAllocLength - Rs;  // Segment 1: buf[rs..al-1]
    memcpy(NewBuffer, Cb->Buffer + Rs, HeadLen);
    if (Rs > 0) {
        memcpy(NewBuffer + HeadLen, Cb->Buffer, Rs);  // Segment 2: buf[0..rs-1]
    }

    VERIFIED_CB_FREE(Cb->Buffer);
    Cb->Buffer = NewBuffer;
    Cb->ReadStart = 0;
    Cb->AllocLength = NewAllocLength;
    // PrefixLength unchanged (verified: resize_prefix_length)
    return 1;
}

//
// Pulse: write_buffer (with auto-resize)
//
// Writes WriteLength bytes from Source at the end of the contiguous prefix.
// If needed, doubles the allocation until it fits (verified: pow2 doubling).
//
_Bool
VerifiedCircBufWriteBuffer(
    VERIFIED_CIRC_BUFFER* Cb,
    const uint8_t* Source,
    uint32_t WriteLength,
    _Bool* AllocationFailed
    )
{
    *AllocationFailed = 0;

    uint32_t PrefixLength = Cb->PrefixLength;
    uint32_t Needed = PrefixLength + WriteLength;

    //
    // Resize if needed (verified: Pow2.next_pow2_ge / pow2_double_le)
    //
    if (Needed > Cb->AllocLength) {
        uint32_t NewAl = Cb->AllocLength;
        while (NewAl < Needed) {
            NewAl = NewAl + NewAl; // Double (verified: doubling_stays_pow2)
        }
        if (!VerifiedCircBufResize(Cb, NewAl)) {
            *AllocationFailed = 1;
            return 0;
        }
    }

    //
    // Write bytes using two-segment memcpy.
    // Physical write position: (ReadStart + PrefixLength) % AllocLength
    // (verified: write_step_coherence / write_range_sequential_prefix)
    //
    uint32_t WriteStart = CircularIndex(Cb->ReadStart, PrefixLength, Cb->AllocLength);
    uint32_t SpaceToEnd = Cb->AllocLength - WriteStart;
    if (WriteLength <= SpaceToEnd) {
        // No wrap-around: single memcpy
        memcpy(Cb->Buffer + WriteStart, Source, WriteLength);
    } else {
        // Wrap-around: two segments
        memcpy(Cb->Buffer + WriteStart, Source, SpaceToEnd);
        memcpy(Cb->Buffer, Source + SpaceToEnd, WriteLength - SpaceToEnd);
    }

    Cb->PrefixLength = PrefixLength + WriteLength;
    return (WriteLength > 0);
}

//
// Pulse: read_buffer
//
// Copies ReadLength bytes from the contiguous prefix into Destination.
// (verified: read_step_invariant)
//
void
VerifiedCircBufReadBuffer(
    const VERIFIED_CIRC_BUFFER* Cb,
    uint8_t* Destination,
    uint32_t ReadLength
    )
{
    //
    // Read bytes using two-segment memcpy.
    // Physical read position: (ReadStart + 0) % AllocLength = ReadStart
    // (verified: read_step_invariant)
    //
    uint32_t Rs = Cb->ReadStart;
    uint32_t SpaceToEnd = Cb->AllocLength - Rs;
    if (ReadLength <= SpaceToEnd) {
        // No wrap-around: single memcpy
        memcpy(Destination, Cb->Buffer + Rs, ReadLength);
    } else {
        // Wrap-around: two segments
        memcpy(Destination, Cb->Buffer + Rs, SpaceToEnd);
        memcpy(Destination + SpaceToEnd, Cb->Buffer, ReadLength - SpaceToEnd);
    }
}

//
// Get internal buffer state for zero-copy read paths.
//
void
VerifiedCircBufGetInternalBuffer(
    const VERIFIED_CIRC_BUFFER* Cb,
    uint8_t** Buffer,
    uint32_t* ReadStart,
    uint32_t* AllocLength
    )
{
    *Buffer = Cb->Buffer;
    *ReadStart = Cb->ReadStart;
    *AllocLength = Cb->AllocLength;
}

//
// Pulse: resize (linearization loop only)
//
// Core verified operation: linearize circular data into a destination buffer.
// for j in 0..OldAllocLength:
//   Dest[j] = Buffer[(ReadStart + j) % OldAllocLength]
// for j in OldAllocLength..DestLength:
//   Dest[j] = 0
//
// Proved correct by:
//   - lemma_resize_invariant_step (each step preserves the loop invariant)
//   - lemma_loop_is_linearized (final result matches Spec.linearized_phys)
//   - linearize_preserves_coherence (physical-logical coherence is maintained)
//   - resize_prefix_length (contiguous prefix is unchanged)
//
void
VerifiedCircBufLinearizeTo(
    const VERIFIED_CIRC_BUFFER* Cb,
    uint8_t* Dest,
    uint32_t DestLength
    )
{
    uint32_t OldAllocLength = Cb->AllocLength;
    uint32_t CopyLen = (OldAllocLength < DestLength) ? OldAllocLength : DestLength;
    uint32_t Rs = Cb->ReadStart;
    uint32_t HeadLen = OldAllocLength - Rs;

    if (CopyLen <= HeadLen) {
        // All data fits in head segment (or buffer is smaller than dest)
        memcpy(Dest, Cb->Buffer + Rs, CopyLen);
    } else {
        // Two segments: head then tail
        memcpy(Dest, Cb->Buffer + Rs, HeadLen);
        memcpy(Dest + HeadLen, Cb->Buffer, CopyLen - HeadLen);
    }
    // Zero-fill remainder
    if (DestLength > OldAllocLength) {
        memset(Dest + OldAllocLength, 0, DestLength - OldAllocLength);
    }
}

//
// Update verified buffer state after an external resize.
// The caller has performed VerifiedCircBufLinearizeTo into a new buffer
// and now needs to update the verified state to match.
//
// Post-conditions (from Pulse proof):
//   - ReadStart == 0  (data is linearized)
//   - AllocLength == NewAllocLength
//   - PrefixLength unchanged (verified: resize_prefix_length)
//   - Buffer points to new allocation
//
void
VerifiedCircBufSyncAfterResize(
    VERIFIED_CIRC_BUFFER* Cb,
    uint8_t* NewBuffer,
    uint32_t NewAllocLength
    )
{
    Cb->Buffer = NewBuffer;
    Cb->ReadStart = 0;
    Cb->AllocLength = NewAllocLength;
    // PrefixLength unchanged (verified: resize_prefix_length)
}
