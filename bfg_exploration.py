#! /usr/bin/env python3

import sys
import numpy as np
from PIL import Image
import zlib

# ------------------------------
# Predictor functions
# ------------------------------
predictors = [
    lambda arr, c, x, y: int(arr[y, x-1, c]) if x > 0 else 0,        # horizontal
    lambda arr, c, x, y: int(arr[y-1, x, c]) if y > 0 else 0,        # vertical
    lambda arr, c, x, y: (int(arr[y-1, x, c]) if y > 0 else 0 +
                           int(arr[y, x-1, c]) if x > 0 else 0) // 2  # mean
]

# ------------------------------
# Encoding function
# ------------------------------
def encode_image(arr, block_size=16):
    height, width, channels = arr.shape
    residuals = []
    predictor_map = []

    for by in range(0, height, block_size):
        for bx in range(0, width, block_size):
            for c in range(channels):
                best_block_residual = None
                best_predictor_idx = 0
                for i, pred_func in enumerate(predictors):
                    block_residual = []
                    for y in range(by, min(by + block_size, height)):
                        for x in range(bx, min(bx + block_size, width)):
                            pred = pred_func(arr, c, x, y)
                            block_residual.append(int(arr[y, x, c]) - int(pred))
                    if best_block_residual is None or sum(map(abs, block_residual)) < sum(map(abs, best_block_residual)):
                        best_block_residual = block_residual
                        best_predictor_idx = i
                residuals.extend(best_block_residual)
                predictor_map.append(best_predictor_idx)

    residual_bytes = bytes([(r + 256) % 256 for r in residuals])
    predictor_bytes = bytes(predictor_map)
    compressed = zlib.compress(predictor_bytes + residual_bytes)
    return compressed, predictor_map, residuals, arr.shape

# ------------------------------
# Decoding function
# ------------------------------
def decode_image(compressed, predictor_map, shape, block_size=16):
    height, width, channels = shape
    decompressed = zlib.decompress(compressed)
    # Recover residuals
    residual_bytes = decompressed[len(predictor_map):]
    residuals = [int(b if b < 128 else b - 256) for b in residual_bytes]
    
    arr = np.zeros((height, width, channels), dtype=np.uint8)
    idx = 0
    block_idx = 0
    for by in range(0, height, block_size):
        for bx in range(0, width, block_size):
            for c in range(channels):
                pred_func = predictors[predictor_map[block_idx]]
                block_idx += 1
                for y in range(by, min(by + block_size, height)):
                    for x in range(bx, min(bx + block_size, width)):
                        pred = pred_func(arr, c, x, y)
                        val = (int(pred) + residuals[idx]) % 256
                        arr[y, x, c] = val
                        idx += 1
    return arr

# ------------------------------
# Main
# ------------------------------
def main():
    if len(sys.argv) < 2:
        print("Usage: python rgb_block_predictor.py image1.png image2.jpg ...")
        return

    for img_path in sys.argv[1:]:
        img = Image.open(img_path).convert("RGB")
        arr = np.array(img, dtype=np.uint8)

        # Encode
        compressed, predictor_map, residuals, shape = encode_image(arr)

        # Decode
        decoded_arr = decode_image(compressed, predictor_map, shape)

        # Verify lossless
        assert np.array_equal(arr, decoded_arr), f"Lossless check failed for {img_path}"

        # Compare sizes
        png_size = len(open(img_path, "rb").read())
        custom_size = len(compressed)

        print(f"{img_path}: PNG size = {png_size} bytes, custom predictor + zlib = {custom_size} bytes, lossless = True")
        print(f"Compression ratio: {custom_size / png_size}")

if __name__ == "__main__":
    main()