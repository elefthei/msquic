/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

Abstract:

    Formally verified circular buffer implementation.

    This C code is a direct translation of the Pulse (F*) verified
    circular buffer in:
      pulse/lib/pulse/lib/Pulse.Lib.CircularBuffer.fst

    The Pulse proofs guarantee:
      - All array accesses are in bounds
      - Resize correctly linearizes wrapped data
      - Drain correctly advances the read position with modulo
      - Write operations maintain physical-logical coherence
      - The no-overcommit theorem: writes within virtual length always
        have a sufficient power-of-2 allocation size

    Platform assumption: size_t is at least 64 bits (true on all
    MsQuic targets).

--*/

#ifndef CIRCULAR_BUFFER_VERIFIED_H
#define CIRCULAR_BUFFER_VERIFIED_H

#include <stdint.h>
#include <stddef.h>

//
// SAL annotation stubs for non-MSVC compilers.
//
#ifndef _In_
#define _In_
#define _Out_
#define _Inout_
#define _In_reads_(x)
#define _Out_writes_(x)
#define _VERIFIED_CB_SAL_STUBS_DEFINED
#endif

#if defined(__cplusplus)
extern "C" {
#endif

//
// Verified circular buffer handle.
// Fields map 1:1 to the Pulse.Lib.CircularBuffer.circular_buffer type.
//
typedef struct VERIFIED_CIRC_BUFFER {
    uint8_t* Buffer;        // Physical backing array  (Pulse: buf/vec)
    uint32_t ReadStart;     // Read position in Buffer (Pulse: rs)
    uint32_t AllocLength;   // Buffer size, always pow2 (Pulse: al)
    uint32_t PrefixLength;  // Contiguous readable prefix (Pulse: pl)
    uint32_t VirtualLength; // Max advertised size, immutable pow2 (Pulse: vl)
} VERIFIED_CIRC_BUFFER;

//
// Initialize a verified circular buffer.
// AllocLength and VirtualLength must be positive powers of 2.
// AllocLength <= VirtualLength.
// Returns TRUE on success.
//
_Bool
VerifiedCircBufInitialize(
    _Out_ VERIFIED_CIRC_BUFFER* Cb,
    _In_ uint32_t AllocLength,
    _In_ uint32_t VirtualLength
    );

//
// Free the buffer's internal allocation.
//
void
VerifiedCircBufUninitialize(
    _Inout_ VERIFIED_CIRC_BUFFER* Cb
    );

//
// Write a single byte at a logical offset within the allocation.
// offset < AllocLength.
// Caller provides the new prefix length (from gap tracking).
//
void
VerifiedCircBufWriteByte(
    _Inout_ VERIFIED_CIRC_BUFFER* Cb,
    _In_ uint32_t Offset,
    _In_ uint8_t Byte,
    _In_ uint32_t NewPrefixLength
    );

//
// Read a single byte at a logical offset within the contiguous prefix.
// Offset < PrefixLength && Offset < AllocLength.
//
uint8_t
VerifiedCircBufReadByte(
    _In_ const VERIFIED_CIRC_BUFFER* Cb,
    _In_ uint32_t Offset
    );

//
// Get the contiguous prefix length (readable data length).
//
uint32_t
VerifiedCircBufGetPrefixLength(
    _In_ const VERIFIED_CIRC_BUFFER* Cb
    );

//
// Get the current allocation length.
//
uint32_t
VerifiedCircBufGetAllocLength(
    _In_ const VERIFIED_CIRC_BUFFER* Cb
    );

//
// Drain n bytes from the front of the buffer.
// n <= AllocLength && n <= PrefixLength.
//
void
VerifiedCircBufDrain(
    _Inout_ VERIFIED_CIRC_BUFFER* Cb,
    _In_ uint32_t DrainLength
    );

//
// Resize (grow) the buffer to NewAllocLength.
// NewAllocLength must be a power of 2, >= current AllocLength,
// <= VirtualLength.
// Data is linearized (unwrapped) into the new buffer.
// Returns TRUE on success, FALSE on allocation failure.
//
_Bool
VerifiedCircBufResize(
    _Inout_ VERIFIED_CIRC_BUFFER* Cb,
    _In_ uint32_t NewAllocLength
    );

//
// Write a contiguous buffer of bytes at the end of the contiguous prefix.
// Auto-resizes if needed.
// Requires the buffer to be gapless (all positions after prefix are empty).
// PrefixLength + WriteLength <= VirtualLength.
// Returns TRUE if new data was written (WriteLength > 0), FALSE otherwise.
// Returns FALSE and sets *AllocationFailed if a resize allocation fails.
//
_Bool
VerifiedCircBufWriteBuffer(
    _Inout_ VERIFIED_CIRC_BUFFER* Cb,
    _In_reads_(WriteLength) const uint8_t* Source,
    _In_ uint32_t WriteLength,
    _Out_ _Bool* AllocationFailed
    );

//
// Read (copy) the contiguous prefix into a destination buffer.
// ReadLength <= PrefixLength && ReadLength <= AllocLength.
//
void
VerifiedCircBufReadBuffer(
    _In_ const VERIFIED_CIRC_BUFFER* Cb,
    _Out_writes_(ReadLength) uint8_t* Destination,
    _In_ uint32_t ReadLength
    );

//
// Get the internal buffer pointer and read start position.
// Used for zero-copy read paths.
//
void
VerifiedCircBufGetInternalBuffer(
    _In_ const VERIFIED_CIRC_BUFFER* Cb,
    _Out_ uint8_t** Buffer,
    _Out_ uint32_t* ReadStart,
    _Out_ uint32_t* AllocLength
    );

//
// Verified linearization: copy the circular buffer into a linear destination.
// This is the core verified resize operation.
// Copies OldAllocLength bytes from the circular layout into Dest[0..OldAllocLength-1],
// zero-fills Dest[OldAllocLength..DestLength-1].
// (Verified: lemma_loop_is_linearized, lemma_resize_invariant_step)
//
void
VerifiedCircBufLinearizeTo(
    _In_ const VERIFIED_CIRC_BUFFER* Cb,
    _Out_writes_(DestLength) uint8_t* Dest,
    _In_ uint32_t DestLength
    );

//
// Update the verified buffer state after an external resize.
// Called after VerifiedCircBufLinearizeTo + external buffer replacement.
//
void
VerifiedCircBufSyncAfterResize(
    _Inout_ VERIFIED_CIRC_BUFFER* Cb,
    _In_ uint8_t* NewBuffer,
    _In_ uint32_t NewAllocLength
    );

#ifdef _VERIFIED_CB_SAL_STUBS_DEFINED
#undef _In_
#undef _Out_
#undef _Inout_
#undef _In_reads_
#undef _Out_writes_
#undef _VERIFIED_CB_SAL_STUBS_DEFINED
#endif

#if defined(__cplusplus)
}
#endif

#endif /* CIRCULAR_BUFFER_VERIFIED_H */
