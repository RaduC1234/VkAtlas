"""
bake_cubemaps.py
----------------
Run from anywhere:
    python scripts/bake_cubemaps.py

Expects:  assets/cubemaps/*.hdr  (relative to project root)
Outputs:  assets/cubemaps/<name>_irradiance.ktx2
          assets/cubemaps/<name>_prefilter.ktx2

Requires: pip install numpy
          cmft on PATH
"""

import os
import sys
import glob
import struct
import subprocess
import shutil

try:
    import numpy as np
except ImportError:
    sys.exit("pip install numpy")

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INPUT_DIR = os.path.join(ROOT_DIR, "assets", "cubemaps")
OUTPUT_DIR = os.path.join(ROOT_DIR, "assets", "cubemaps")
FACE_SUFFIXES = ["posx", "negx", "posy", "negy", "posz", "negz"]
MIP_COUNT = 6
MIP_SIZES = [128, 64, 32, 16, 8, 4]

# ---------------------------------------------------------------------------
# KTX2 constants
# ---------------------------------------------------------------------------
KTX2_MAGIC = bytes([0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A])
VK_FORMAT_R32G32B32A32_SFLOAT = 109
TYPE_SIZE = 4


# ---------------------------------------------------------------------------
# RGBE loader (pure numpy, no extra deps)
# ---------------------------------------------------------------------------
def load_face(path: str) -> np.ndarray:
    with open(path, "rb") as f:
        raw = f.read()

    # Skip header lines until blank line
    i = 0
    while True:
        eol = raw.index(10, i)  # 10 = ord('\n')
        line = raw[i:eol]
        i = eol + 1
        if line == b"":
            break

    # Resolution line: e.g. "-Y 32 +X 32"
    eol = raw.index(10, i)
    res = raw[i:eol].decode().split()
    i = eol + 1
    height = int(res[1])
    width = int(res[3])

    # Raw RGBE pixels
    data = np.frombuffer(raw[i:], dtype=np.uint8).reshape(height, width, 4)
    exp = data[:, :, 3].astype(np.int32) - 128 - 8
    scale = np.ldexp(1.0, exp).astype(np.float32)
    rgb = data[:, :, :3].astype(np.float32) * scale[:, :, np.newaxis]
    alpha = np.ones((height, width, 1), dtype=np.float32)
    return np.concatenate([rgb, alpha], axis=2)  # (H, W, 4) float32


# ---------------------------------------------------------------------------
# KTX2 helpers
# ---------------------------------------------------------------------------
def align(o: int, n: int) -> int:
    return (o + n - 1) & ~(n - 1)


def pad_to(o: int, n: int) -> bytes:
    return b"\x00" * (align(o, n) - o)


def build_dfd() -> bytes:
    # Minimal valid DFD for VK_FORMAT_R32G32B32A32_SFLOAT
    # totalSize | vendorId=0 | descriptorType=0 | versionNumber=2 | descriptorBlockSize
    desc_block_size = 24 + 4 * 16  # header + 4 samples
    total_dfd_size = 4 + desc_block_size

    buf = bytearray()
    buf += struct.pack("<I", total_dfd_size)  # totalSize
    buf += struct.pack("<I", 0)  # vendorId=0, descriptorType=0
    buf += struct.pack("<H", 2)  # versionNumber
    buf += struct.pack("<H", desc_block_size)  # descriptorBlockSize
    buf += struct.pack("B", 1)  # colorModel: RGBSDA
    buf += struct.pack("B", 1)  # colorPrimaries: BT709
    buf += struct.pack("B", 1)  # transferFunction: LINEAR
    buf += struct.pack("B", 0)  # flags
    # texelBlockDimension0..3 (all 0 = 1x1x1x1 block)
    buf += struct.pack("BBBB", 0, 0, 0, 0)
    # bytesPlane0=16, bytesPlane1..7=0
    buf += struct.pack("BBBBBBBB", 16, 0, 0, 0, 0, 0, 0, 0)

    LOWER = struct.unpack("<I", struct.pack("<f", -1.0))[0]
    UPPER = struct.unpack("<I", struct.pack("<f", 1.0))[0]
    for ch_id, bit_offset in [(0, 0), (1, 32), (2, 64), (15, 96)]:
        buf += struct.pack("<H", bit_offset)  # bitOffset
        buf += struct.pack("B", 31)  # bitLength (32 bits - 1)
        buf += struct.pack("B", (ch_id & 0x0F) | 0xC0)  # channelType: float+signed
        buf += struct.pack("<I", 0)  # samplePosition
        buf += struct.pack("<I", LOWER)
        buf += struct.pack("<I", UPPER)

    return bytes(buf)

def write_ktx2(mip_faces: list, out_path: str) -> None:
    num_mips = len(mip_faces)
    face_size = mip_faces[0][0].shape[0]

    mip_bytes = [b"".join(f.astype("<f4").tobytes() for f in mip_faces[i]) for i in range(num_mips)]
    mip_bytes_rev = list(reversed(mip_bytes))  # KTX2: smallest mip first on disk

    dfd = build_dfd()

    HEADER_SIZE = 80
    level_index_size = num_mips * 24
    dfd_offset = HEADER_SIZE + level_index_size
    after_dfd = dfd_offset + len(dfd)
    data_start = align(after_dfd, 8)

    mip_offsets, cursor = [], data_start
    for mb in mip_bytes_rev:
        cursor = align(cursor, 16)
        mip_offsets.append(cursor)
        cursor += len(mb)

    out = bytearray()
    out += KTX2_MAGIC
    out += struct.pack("<IIIII", VK_FORMAT_R32G32B32A32_SFLOAT, TYPE_SIZE, face_size, face_size, 0)
    out += struct.pack("<IIII", 0, 6, num_mips, 0)
    out += struct.pack("<IIII", dfd_offset, len(dfd), 0, 0)  # kvdByteOffset=0 when no KVD
    out += struct.pack("<QQ", 0, 0)
    assert len(out) == HEADER_SIZE

    # FIX: level index entry 0 = mip level 0 = largest mip,
    # but data on disk is stored smallest-first.
    # Map level i -> reversed index (num_mips - 1 - i).
    for i in range(num_mips):
        ri = num_mips - 1 - i
        out += struct.pack("<QQQ", mip_offsets[ri], len(mip_bytes_rev[ri]), len(mip_bytes_rev[ri]))

    out += dfd

    for i, mb in enumerate(mip_bytes_rev):
        out += pad_to(len(out), 16)
        out += mb

    with open(out_path, "wb") as f:
        f.write(out)
    print(f"    Written: {out_path}  ({len(out):,} bytes, {num_mips} mip(s))")


# ---------------------------------------------------------------------------
# Face loaders / cleanup
# ---------------------------------------------------------------------------
def load_irradiance_faces(prefix: str) -> list:
    faces = []
    for s in FACE_SUFFIXES:
        p = f"{prefix}_{s}.hdr"
        if not os.path.exists(p):
            sys.exit(f"Missing irradiance face: {p}")
        faces.append(load_face(p))
    return [faces]


def load_prefilter_faces(prefix: str) -> list:
    levels = []
    for mip, size in enumerate(MIP_SIZES):
        faces = []
        for s in FACE_SUFFIXES:
            p = f"{prefix}_{s}_{mip}_{size}x{size}.hdr"
            if not os.path.exists(p):
                sys.exit(f"Missing prefilter face: {p}")
            faces.append(load_face(p))
        levels.append(faces)
    print(f"    Found {len(levels)} mip level(s)")
    return levels


def delete_irradiance_faces(prefix: str) -> None:
    for s in FACE_SUFFIXES:
        p = f"{prefix}_{s}.hdr"
        if os.path.exists(p):
            os.remove(p)


def delete_prefilter_faces(prefix: str) -> None:
    for mip, size in enumerate(MIP_SIZES):
        for s in FACE_SUFFIXES:
            p = f"{prefix}_{s}_{mip}_{size}x{size}.hdr"
            if os.path.exists(p):
                os.remove(p)


# ---------------------------------------------------------------------------
# cmft runner
# ---------------------------------------------------------------------------
def run_cmft(args: list) -> None:
    result = subprocess.run(["cmft"] + args, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        sys.exit(f"cmft failed (exit {result.returncode})")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    if not shutil.which("cmft"):
        sys.exit("cmft not found on PATH")

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    hdrs = sorted(glob.glob(os.path.join(INPUT_DIR, "*.hdr")))
    hdrs = [h for h in hdrs if not any(
        tag in os.path.basename(h) for tag in ("_irradiance_", "_prefilter_")
    )]

    if not hdrs:
        sys.exit(f"No source HDR files found in {INPUT_DIR}")

    for hdr_path in hdrs:
        name = os.path.splitext(os.path.basename(hdr_path))[0]
        irr_prefix = os.path.join(OUTPUT_DIR, f"{name}_irradiance")
        pre_prefix = os.path.join(OUTPUT_DIR, f"{name}_prefilter")
        irr_ktx2 = irr_prefix + ".ktx2"
        pre_ktx2 = pre_prefix + ".ktx2"

        print(f"\n{'=' * 60}")
        print(f"  {name}")
        print(f"{'=' * 60}")

        if os.path.exists(irr_ktx2):
            print(f"  [irradiance] already exists, skipping.")
        else:
            print(f"  [1/2] cmft irradiance...")
            run_cmft([
                "--input", hdr_path,
                "--filter", "irradiance",
                "--dstFaceSize", "32",
                "--outputNum", "1",
                "--output0", irr_prefix,
                "--output0params", "hdr,rgbe,facelist",
            ])
            print(f"  [1/2] packing irradiance to KTX2...")
            write_ktx2(load_irradiance_faces(irr_prefix), irr_ktx2)
            delete_irradiance_faces(irr_prefix)

        if os.path.exists(pre_ktx2):
            print(f"  [prefilter]  already exists, skipping.")
        else:
            print(f"  [2/2] cmft prefilter...")
            run_cmft([
                "--input", hdr_path,
                "--filter", "radiance",
                "--glossScale", "10",
                "--glossBias", "3",
                "--mipCount", str(MIP_COUNT),
                "--dstFaceSize", str(MIP_SIZES[0]),
                "--outputNum", "1",
                "--output0", pre_prefix,
                "--output0params", "hdr,rgbe,facelist",
            ])
            print(f"  [2/2] packing prefilter to KTX2...")
            write_ktx2(load_prefilter_faces(pre_prefix), pre_ktx2)
            delete_prefilter_faces(pre_prefix)

    print(f"\nAll done.")


if __name__ == "__main__":
    main()
