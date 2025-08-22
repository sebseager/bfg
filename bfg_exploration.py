#! /usr/bin/env python3

import sys
import numpy as np
from PIL import Image
import zlib

# ------------------------------
# Paeth predictor
# ------------------------------
def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    elif pb <= pc:
        return b
    else:
        return c

predictors = [
    lambda arr, c, x, y: paeth(
        arr[y, x-1, c] if x > 0 else 0,
        arr[y-1, x, c] if y > 0 else 0,
        arr[y-1, x-1, c] if x > 0 and y > 0 else 0
    )
]

# ------------------------------
# Integer wavelet transform (CDF 5/3)
# ------------------------------
def iwt_2d(block):
    """Apply 2D integer wavelet transform (CDF 5/3) to a block."""
    block = block.astype(np.int32)
    h, w = block.shape

    # Horizontal pass
    for y in range(h):
        for x in range(1, w-1, 2):
            block[y, x] -= (block[y, x-1] + block[y, x+1]) // 2
        for x in range(0, w, 2):
            block[y, x] += (block[y, x-1] + block[y, x+1] + 2) // 4 if 0 < x < w-1 else 0

    # Vertical pass
    for x in range(w):
        for y in range(1, h-1, 2):
            block[y, x] -= (block[y-1, x] + block[y+1, x]) // 2
        for y in range(0, h, 2):
            block[y, x] += (block[y-1, x] + block[y+1, x] + 2) // 4 if 0 < y < h-1 else 0

    return block

def iwt_2d_inverse(block):
    """Inverse 2D integer wavelet transform."""
    block = block.astype(np.int32)
    h, w = block.shape

    # Vertical inverse
    for x in range(w):
        for y in range(0, h, 2):
            block[y, x] -= (block[y-1, x] + block[y+1, x] + 2) // 4 if 0 < y < h-1 else 0
        for y in range(1, h-1, 2):
            block[y, x] += (block[y-1, x] + block[y+1, x]) // 2

    # Horizontal inverse
    for y in range(h):
        for x in range(0, w, 2):
            block[y, x] -= (block[y-1, x] + block[y+1, x] + 2) // 4 if 0 < x < w-1 else 0
        for x in range(1, w-1, 2):
            block[y, x] += (block[y, x-1] + block[y, x+1]) // 2

    return block

# ------------------------------
# Encoding function
# ------------------------------
def encode_image(arr, block_size=16):
    height, width, channels = arr.shape
    residuals = []

    for by in range(0, height, block_size):
        for bx in range(0, width, block_size):
            for c in range(channels):
                block = arr[by:by+block_size, bx:bx+block_size, c].astype(np.int32)
                # Apply predictor
                pred_block = np.zeros_like(block)
                for y in range(block.shape[0]):
                    for x in range(block.shape[1]):
                        pred_block[y, x] = predictors[0](arr, c, bx + x, by + y)
                residual_block = block - pred_block
                # Apply integer wavelet
                residual_block = iwt_2d(residual_block)
                residuals.extend(residual_block.flatten())

    residual_bytes = bytes([(r + 256) % 256 for r in residuals])
    compressed = zlib.compress(residual_bytes)
    return compressed, arr.shape

# ------------------------------
# Decoding function
# ------------------------------
def decode_image(compressed, shape, block_size=16):
    height, width, channels = shape
    decompressed = zlib.decompress(compressed)
    residuals = [int(b if b < 128 else b - 256) for b in decompressed]

    arr = np.zeros((height, width, channels), dtype=np.uint8)
    idx = 0

    for by in range(0, height, block_size):
        for bx in range(0, width, block_size):
            for c in range(channels):
                h_block = min(block_size, height - by)
                w_block = min(block_size, width - bx)
                block_flat = residuals[idx: idx + h_block * w_block]
                idx += h_block * w_block
                block = np.array(block_flat, dtype=np.int32).reshape((h_block, w_block))
                # Inverse integer wavelet
                block = iwt_2d_inverse(block)
                # Apply predictor
                for y in range(h_block):
                    for x in range(w_block):
                        pred = predictors[0](arr, c, bx + x, by + y)
                        val = (pred + block[y, x]) % 256
                        arr[by + y, bx + x, c] = val

    return arr

# ------------------------------
# Main
# ------------------------------
def main():
    if len(sys.argv) < 2:
        print("Usage: python rgb_block_predictor_iwt.py image1.png image2.jpg ...")
        return

    for img_path in sys.argv[1:]:
        img = Image.open(img_path).convert("RGB")
        arr = np.array(img, dtype=np.uint8)

        # Encode
        compressed, shape = encode_image(arr)

        # Decode
        decoded_arr = decode_image(compressed, shape)

        # Verify lossless
        assert np.array_equal(arr, decoded_arr), f"Lossless check failed for {img_path}"

        # Compare sizes
        png_size = len(open(img_path, "rb").read())
        custom_size = len(compressed)

        print(f"{img_path}: PNG size = {png_size} bytes, custom predictor+IWT+zlib = {custom_size} bytes, lossless = True")
        print(f"Compression ratio: {custom_size / png_size:.3f}")

if __name__ == "__main__":
    main()