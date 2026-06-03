"""
BRDF LUT Generator — Python port of the C++ original.
Generates a 512x512 split-sum BRDF look-up table and writes it as a
Radiance HDR file (brdf_lut.hdr), identical output to the C++ version.
"""

import math
import struct
import time

LUT_SIZE    = 512
NUM_SAMPLES = 1024


# ---------------------------------------------------------------------------
# Low-discrepancy sampling
# ---------------------------------------------------------------------------

def radical_inverse_vdc(bits: int) -> float:
    bits = ((bits << 16) | (bits >> 16)) & 0xFFFFFFFF
    bits = (((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1)) & 0xFFFFFFFF
    bits = (((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2)) & 0xFFFFFFFF
    bits = (((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4)) & 0xFFFFFFFF
    bits = (((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8)) & 0xFFFFFFFF
    return bits / 0x100000000


def hammersley(i: int, n: int) -> tuple[float, float]:
    return i / n, radical_inverse_vdc(i)


# ---------------------------------------------------------------------------
# GGX importance sampling
# ---------------------------------------------------------------------------

def importance_sample_ggx(xi: tuple[float, float], roughness: float) -> tuple[float, float, float]:
    a         = roughness * roughness
    phi       = 2.0 * math.pi * xi[0]
    cos_theta = math.sqrt((1.0 - xi[1]) / max(1.0 + (a * a - 1.0) * xi[1], 1e-38))
    sin_theta = math.sqrt(max(1.0 - cos_theta * cos_theta, 0.0))
    return math.cos(phi) * sin_theta, math.sin(phi) * sin_theta, cos_theta


# ---------------------------------------------------------------------------
# Geometry term (Smith / Schlick-GGX, IBL variant)
# ---------------------------------------------------------------------------

def geometry_schlick_ggx(ndotv: float, roughness: float) -> float:
    k = (roughness * roughness) / 2.0
    return ndotv / (ndotv * (1.0 - k) + k)


def geometry_smith(ndotv: float, ndotl: float, roughness: float) -> float:
    return geometry_schlick_ggx(ndotv, roughness) * geometry_schlick_ggx(ndotl, roughness)


# ---------------------------------------------------------------------------
# Core integration
# ---------------------------------------------------------------------------

def integrate_brdf(ndotv: float, roughness: float) -> tuple[float, float]:
    ndotv = max(ndotv, 0.001)

    vx = math.sqrt(1.0 - ndotv * ndotv)
    vy = 0.0
    vz = ndotv

    scale = 0.0
    bias  = 0.0

    for i in range(NUM_SAMPLES):
        xi = hammersley(i, NUM_SAMPLES)
        hx, hy, hz = importance_sample_ggx(xi, roughness)

        vdoth = max(vx * hx + vy * hy + vz * hz, 0.0)

        lx = 2.0 * vdoth * hx - vx
        ly = 2.0 * vdoth * hy - vy
        lz = 2.0 * vdoth * hz - vz

        ndotl = max(lz, 0.0)
        ndoth = max(hz, 0.0)

        if ndotl > 0.0:
            g    = geometry_smith(ndotv, ndotl, roughness)
            gvis = (g * vdoth) / max(ndoth * ndotv, 0.001)
            fc   = (1.0 - vdoth) ** 5.0

            scale += gvis * (1.0 - fc)
            bias  += gvis * fc

    inv = 1.0 / NUM_SAMPLES
    return scale * inv, bias * inv


# ---------------------------------------------------------------------------
# RGBE encoding
# ---------------------------------------------------------------------------

def encode_rgbe(r: float, g: float, b: float) -> bytes:
    max_val = max(r, g, b)
    if max_val <= 1e-32:
        return b'\x00\x00\x00\x00'
    m, exp = math.frexp(max_val)
    scale  = m * 256.0 / max_val
    return bytes([
        min(int(r * scale), 255),
        min(int(g * scale), 255),
        min(int(b * scale), 255),
        exp + 128,
        ])


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    print(f"Generating BRDF LUT ({LUT_SIZE}x{LUT_SIZE}, {NUM_SAMPLES} samples per texel)")

    start = time.perf_counter()

    lut: list[tuple[float, float]] = [None] * (LUT_SIZE * LUT_SIZE)  # type: ignore[list-item]

    for y in range(LUT_SIZE):
        roughness = max((y + 0.5) / LUT_SIZE, 0.01)

        for x in range(LUT_SIZE):
            ndotv      = (x + 0.5) / LUT_SIZE
            lut[y * LUT_SIZE + x] = integrate_brdf(ndotv, roughness)

        if (y + 1) % 64 == 0:
            elapsed  = time.perf_counter() - start
            progress = (y + 1) / LUT_SIZE
            eta      = elapsed / progress * (1.0 - progress)
            print(f"  {progress * 100:3.0f}% done, ETA: {eta:.1f}s")

    elapsed = time.perf_counter() - start
    print(f"Done in {elapsed:.1f}s")

    # -----------------------------------------------------------------------
    # Write Radiance HDR
    # -----------------------------------------------------------------------
    path = "brdf_lut.hdr"
    with open(path, "wb") as f:
        # Header
        f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n")
        f.write(f"-Y {LUT_SIZE} +X {LUT_SIZE}\n".encode())

        # Scanlines — raw RGBE, no RLE
        for y in range(LUT_SIZE):
            for x in range(LUT_SIZE):
                r, g = lut[y * LUT_SIZE + x]
                f.write(encode_rgbe(r, g, 0.0))

    print(f"Saved: {path}")
    print(f"Format: {LUT_SIZE}x{LUT_SIZE}, RG encoded as HDR (B=0)")


if __name__ == "__main__":
    main()