/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

Abstract:

    Formally verified circular buffer operations, extracted from
    Pulse.Lib.CircularBuffer via KaRaMeL.

    This file is #include'd directly by recv_buffer.c (not compiled separately)
    so it can use the VERIFIED_CB_ALLOC / VERIFIED_CB_FREE macros defined there.

    =====================================================================
    EXTRACTED CODE — adapted from KaRaMeL output of:
      Pulse.Lib.CircularBuffer.{drain, resize}
    Source: ~/pulse/lib/pulse/lib/Pulse.Lib.CircularBuffer.fst
    Wrapper: /tmp/cb_extract2/_wrapper_src/CircBufWrap.fst
    =====================================================================

--*/

//
// VerifiedCircBufLinearizeTo
//
// Extracted from: Pulse.Lib.CircularBuffer.resize (linearization segment)
//
// Copies the circular buffer contents into a linear destination buffer,
// unwrapping the wrap-around. This is the two-memcpy form proven equivalent
// to the byte-by-byte loop by Spec.linearized_phys_two_segments.
//
// Pre:  DstLength >= Buf->AllocLength
// Post: dst[0..al-rs) = src[rs..al), dst[al-rs..al) = src[0..rs)
//       (remaining dst bytes are untouched / zero-initialized by caller)
//
void
VerifiedCircBufLinearizeTo(
    VERIFIED_CIRC_BUFFER* Buf,
    uint8_t* DstBuffer,
    uint32_t DstLength)
{
    UNREFERENCED_PARAMETER(DstLength);
    //
    // Extracted from Pulse.Lib.CircularBuffer.resize:
    //   let head_len = al - rs;
    //   memcpy(new_ptr, old_head_ptr, head_len);  // old[rs..al) → new[0..head_len)
    //   memcpy(new_tail_ptr, old_ptr, rs);         // old[0..rs)  → new[head_len..al)
    //
    uint32_t rs = Buf->ReadStart;
    uint32_t al = Buf->AllocLength;
    uint32_t head_len = al - rs;

    if (head_len > 0) {
        memcpy(DstBuffer, Buf->Buffer + rs, head_len);
    }
    if (rs > 0) {
        memcpy(DstBuffer + head_len, Buf->Buffer, rs);
    }
}

//
// VerifiedCircBufSyncAfterResize
//
// Extracted from: Pulse.Lib.CircularBuffer.resize (state update segment)
//
// After linearization, update the verified buffer to point at the new
// (linear) allocation with ReadStart reset to 0.
//
void
VerifiedCircBufSyncAfterResize(
    VERIFIED_CIRC_BUFFER* Buf,
    uint8_t* NewBuffer,
    uint32_t NewAllocLength)
{
    //
    // Extracted from Pulse.Lib.CircularBuffer.resize:
    //   *cb.buf = new_vec;
    //   *cb.rs  = 0;
    //   *cb.al  = new_al;
    //
    Buf->Buffer = NewBuffer;
    Buf->ReadStart = 0;
    Buf->AllocLength = NewAllocLength;
}

//
// VerifiedCircBufDrain
//
// Extracted from: Pulse.Lib.CircularBuffer.drain
//
// Advances the read head by DrainLength positions (modular) and
// decreases PrefixLength accordingly.
//
// Pre:  DrainLength <= PrefixLength
//       DrainLength <= AllocLength
// Post: ReadStart' = (ReadStart + DrainLength) % AllocLength
//       PrefixLength' = PrefixLength - DrainLength
//
void
VerifiedCircBufDrain(
    VERIFIED_CIRC_BUFFER* Buf,
    uint32_t DrainLength)
{
    //
    // Extracted from Pulse.Lib.CircularBuffer.drain:
    //   let temp   = rs + n;
    //   let new_rs = temp % al;
    //   *cb.rs = new_rs;
    //   let new_pl = pl - n;
    //   *cb.pl = new_pl;
    //
    uint32_t rs = Buf->ReadStart;
    uint32_t al = Buf->AllocLength;
    uint32_t pl = Buf->PrefixLength;

    uint32_t temp = rs + DrainLength;
    uint32_t new_rs = temp % al;
    Buf->ReadStart = new_rs;

    uint32_t new_pl = pl - DrainLength;
    Buf->PrefixLength = new_pl;
}
