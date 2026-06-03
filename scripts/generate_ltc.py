import numpy as np
import os

SIZE = 64

def saturate(x):
    return max(0.0, min(1.0, x))

def ltc_fit(NdotV, roughness):
    NdotV = max(NdotV, 1e-4)
    roughness = max(roughness, 1e-4)

    r = roughness
    ct = NdotV
    st = np.sqrt(max(0.0, 1.0 - ct * ct))

    a  = 1.0 + r * (-0.542028 + r * (0.303242 + r * (-0.122778)))
    a /= 1.0 + r * (-0.665199 + r * (0.295427))

    b  = st * r * (1.0 + r * (-1.044438 + r * 0.321311))
    b /= 1.0 + r * (0.160265)

    d  = 1.0 / max(a, 1e-6)
    c  = 0.0

    amp  = 1.0 - r * r * (0.1 + r * 0.05)
    amp *= 1.0 - (1.0 - ct) * (1.0 - ct) * 0.1

    fres = saturate(np.exp(-5.55473 * (1.0 - ct)) * (1.0 - r) + r * 0.04)

    return float(a), float(b), float(c), float(d), float(amp), float(fres)


ltc_mat = np.zeros((SIZE, SIZE, 4), dtype=np.float32)
ltc_amp = np.zeros((SIZE, SIZE, 4), dtype=np.float32)

for row in range(SIZE):
    for col in range(SIZE):
        roughness = (col + 0.5) / SIZE
        theta     = (1.0 - (row + 0.5) / SIZE) * (np.pi * 0.5)
        NdotV     = np.cos(theta)

        a, b, c, d, amp, fres = ltc_fit(NdotV, roughness)

        ltc_mat[row, col] = [a, b, c, d]
        ltc_amp[row, col] = [amp, fres, 0.0, 1.0]

os.makedirs('../assets/engine', exist_ok=True)
ltc_mat.tofile('../assets/engine/ltc_mat.lut.bin')
ltc_amp.tofile('../assets/engine/ltc_amp.lut.bin')

expected_bytes = SIZE * SIZE * 4 * 4  # 64*64 texels * 4 channels * 4 bytes
print(f"Done. Each file: {expected_bytes} bytes ({SIZE}x{SIZE} x RGBA32F)")