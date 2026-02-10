/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

Abstract:

    Formally verified circular buffer operations, extracted from
    Pulse.Lib.CircularBuffer via KaRaMeL.

    The verified Pulse source proves:
    - Physical-logical coherence (Spec.phys_log_coherent)
    - Drain correctness (Spec.drain_preserves_coherence)
    - Resize/linearize correctness (Spec.linearize_preserves_coherence)
    - Modular index safety (Modular.circular_index_in_bounds)

    Source: ~/pulse/lib/pulse/lib/Pulse.Lib.CircularBuffer.fst

--*/

#pragma once

#include <stdint.h>
#include <string.h>

//
// Flat verified circular buffer state.
// Buffer memory is externally owned (by QUIC_RECV_CHUNK).
//
typedef struct VERIFIED_CIRC_BUFFER {
    uint8_t*  Buffer;        // Physical buffer (externally owned, not freed by us)
    uint32_t  ReadStart;     // Read head position in physical buffer
    uint32_t  AllocLength;   // Physical buffer size (always a power of 2)
    uint32_t  PrefixLength;  // Length of contiguous readable data from ReadStart
    uint32_t  VirtualLength; // Maximum allowed buffer size (always a power of 2)
} VERIFIED_CIRC_BUFFER;

//
// Linearize (copy) the circular buffer contents into a new destination buffer,
// unwrapping the circular layout into a linear one.
// Copies: dst[0..head_len) = src[ReadStart..AllocLength)
//         dst[head_len..AllocLength) = src[0..ReadStart)
//
// Verified by: Spec.linearized_phys_two_segments,
//              lemma_memcpy_is_linearized_elementwise
//
void
VerifiedCircBufLinearizeTo(
    VERIFIED_CIRC_BUFFER* Buf,
    uint8_t* DstBuffer,
    uint32_t DstLength);

//
// Update the verified buffer state after a resize operation.
// Points Buffer at the new (linearized) allocation and resets ReadStart to 0.
//
// Verified by: Spec.linearize_preserves_coherence,
//              Spec.resize_prefix_length
//
void
VerifiedCircBufSyncAfterResize(
    VERIFIED_CIRC_BUFFER* Buf,
    uint8_t* NewBuffer,
    uint32_t NewAllocLength);

//
// Drain (consume) DrainLength bytes from the front of the buffer.
// Advances ReadStart circularly and decreases PrefixLength.
//
// Verified by: Spec.drain_preserves_coherence,
//              Modular.advance_read_start,
//              Spec.drain_prefix_length
//
void
VerifiedCircBufDrain(
    VERIFIED_CIRC_BUFFER* Buf,
    uint32_t DrainLength);
