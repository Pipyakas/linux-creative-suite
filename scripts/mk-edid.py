#!/usr/bin/env python3
"""Generate a valid 128-byte EDID 1.4 blob for this laptop's 1366x768 panel.

Why: the prefix's monitor entry `HKLM\\SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\
Default_Monitor\\0000&0000` has no `EDID` value, so Wine reports
`readMonitorEdidFromKey: Failed to get EDID reg key size` and every DXGI output
comes out with `Physical size: (0 0)` / `Pixel size: (0 0)`.  The suite's own
GPU probe helper prints those zeros for `Display 0`.  A monitor that reports no physical
size has no DPI, and DPI is a plausible thing for the apps to gate on.

Prints the blob as a `wine reg add ... /t REG_BINARY /d ...` argument string.
"""
import sys

def build(width=1366, height=768, h_cm=34, v_cm=19, mfr="DEL", product=0x4321,
          serial=0x12345678, refresh=60):
    e = bytearray(128)

    # 0-7: fixed header
    e[0:8] = bytes([0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00])

    # 8-9: manufacturer ID, big-endian, three 5-bit letters
    m = 0
    for ch in mfr:
        m = (m << 5) | (ord(ch) - ord('A') + 1)
    e[8] = (m >> 8) & 0xFF
    e[9] = m & 0xFF

    # 10-11 product code (little-endian), 12-15 serial (little-endian)
    e[10] = product & 0xFF
    e[11] = (product >> 8) & 0xFF
    for i in range(4):
        e[12 + i] = (serial >> (8 * i)) & 0xFF

    e[16] = 10                       # week
    e[17] = 2026 - 1990              # year
    e[18] = 1                        # EDID version
    e[19] = 4                        # EDID revision

    # 20: video input parameters. Bit7=1 digital; bits 6-4 colour encoding;
    # bits 3-0 bit depth.  0xA0 = digital, standard RGB, 6 bits/colour.
    e[20] = 0xA0
    e[21] = h_cm                     # horizontal size, cm
    e[22] = v_cm                     # vertical size, cm
    e[23] = 120                      # gamma = (gamma+100)/100 -> 2.20
    e[24] = 0x0A                     # DPMS standby / suspend / active

    # 25-34: chromaticity (sRGB-ish defaults from the EDID spec's sample block)
    e[25:35] = bytes([0x78, 0xF0, 0x9C, 0x58, 0x54, 0x8C, 0x27, 0x27, 0x50, 0x1E])

    # 35-37: established timings, unused
    e[35] = e[36] = e[37] = 0x00

    # 38-40: standard timings 1-8. Bit 7 = valid, bits 6-0 = (H-31)*8 for
    # horizontal, and a second byte packed the same way.  1366x768 is not an
    # established standard timing, so mark 1..8 unused (0x01).
    e[38:41] = b"\x01\x01\x01"

    # 41-53: manufacturer-specific and unused standard timings 9-18
    for i in range(41, 54):
        e[i] = 0x01

    # 54-71: detailed timing descriptor for the native mode.
    #   pixel clock / 10 kHz - 1, then the standard h/v timing nibbles.
    pixel_clock_hz = 69_500_000
    pck = pixel_clock_hz // 10_000 - 1

    h_active = width
    h_blank = 160
    h_front = 48
    h_sync = 32
    v_active = height
    v_blank = 24
    v_front = 3
    v_sync = 5

    # Reduce a value into the EDID 10-bit/8-bit fields, tracking remainders.
    def enc(val, ref, bits):
        """Encode val using a reference value, returning (code, remainder)."""
        best = None
        for r in range(ref):
            step = ref + r
            code = (val * (1 << bits)) // step
            if code > (1 << bits) - 1:
                code = (1 << bits) - 1
            err = abs(val - code * step / (1 << bits))
            if best is None or err < best[0]:
                best = (err, code, step)
        return best[1], best[2]

    h_front_code, h_front_ref = enc(h_front, 40, 4)
    h_sync_code, h_sync_ref = enc(h_sync, 40, 4)

    def nib_pair(value, bits, ref):
        """Split a value into upper/lower N-bit fields sharing a reference."""
        code, step = enc(value, ref, bits)
        return code, step

    # Build the descriptor with simple binary (not reduced) timing, which is
    # always legal for a DTD; consumers fall back to the established/standard
    # timings if they prefer.
    def bit(v, n):
        return (v >> n) & 1

    d = bytearray(18)
    d[0] = pck & 0xFF
    d[1] = (pck >> 8) & 0xFF
    # horizontal active lower 8 bits
    d[2] = h_active & 0xFF
    # horizontal blanking lower 8
    d[3] = h_blank & 0xFF
    # bits: [7:4] h_active[11:8]  [3:0] h_blank[11:8]
    d[4] = ((h_active >> 8) << 4) | ((h_blank >> 8) & 0x0F)
    # vertical active lower 8 / vertical blanking lower 8
    d[5] = v_active & 0xFF
    d[6] = v_blank & 0xFF
    d[7] = ((v_active >> 8) << 4) | ((v_blank >> 8) & 0x0F)
    # h sync offset (front porch) lower 8, h sync pulse width lower 8
    d[8] = h_front & 0xFF
    d[9] = h_sync & 0xFF
    # [7:4] v_front[3:0]  [3:0] h_front[7:4]
    d[10] = ((v_front & 0x0F) << 4) | ((h_front >> 4) & 0x0F)
    # [7:4] v_sync[3:0]  [3:0] h_sync[7:4]
    d[11] = ((v_sync & 0x0F) << 4) | ((h_sync >> 4) & 0x0F)
    # Image size in the DTD, split across two bytes:
    #   byte 12: [7:6] H mm [9:8]   [5:4] V mm [9:8]   [3:0] H image offset
    #   byte 13: [7:3] H mm [4:0]   [2:0] V mm [2:0]
    # That field is only 7 bits wide (max 127 mm) and 0 means "unspecified", which is what
    # real EDIDs use for panels wider than that.  The size that actually matters is the
    # Basic Display Parameters above, which are in **cm** and hold this panel's real
    # 34 x 19 cm fine -- that is the field DXGI turns into PhysicalSizeMm.
    d[12] = 0x00
    d[13] = 0x00
    # bit 7 = interlaced, bits 6-5 sync type (digital separate is 3)
    d[14] = (3 << 5)
    # stereo + sync + no extension
    d[15] = 0x00
    d[16] = v_active & 0xFF          # dummy
    d[17] = 0x00

    e[54:72] = d

    # 72-89, 90-107, 108-125: three more descriptors. Mark them as monitor
    # range limits + dummy so nothing is misread.
    # Monitor range limits (offset 0xFD within the descriptor)
    e[72:90] = bytes([0x00, 0x00, 0x00, 0xFD, 0x00, 0x32, 0x4B, 0x1E,
                      0x64, 0x1E, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00])
    e[90:126] = b"\x00" * 36

    e[126] = 0        # no extension blocks
    e[127] = (-sum(e)) & 0xFF          # checksum so the block sums to 0 mod 256
    return bytes(e)


if __name__ == "__main__":
    blob = build()
    print("python-blob-hex:", blob.hex())
    print("wine-reg-binary:", ",".join(str(b) for b in blob))
