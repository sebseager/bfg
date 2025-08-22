import sys
import numpy as np
from matplotlib.image import imread

def bxc_encode_block(block):
    bs, _, c = block.shape  # assume square block
    out = bytearray()
    for ch in range(c):
        rows = []
        for y in range(bs):
            row = 0
            for x in range(bs):
                row |= (int(block[y, x, ch]) << ((bs - 1 - x) * 8))
            rows.append(row)
        out.extend(rows[0].to_bytes(bs, 'big'))
        prev = rows[0]
        for r in rows[1:]:
            xor = r ^ prev
            if xor == 0:
                out.append(0)
            else:
                xor_b = xor.to_bytes(bs, 'big')
                leading_zeros = 0
                for b in xor_b:
                    if b == 0:
                        leading_zeros += 1
                    else:
                        break
                length = bs - leading_zeros
                out.append(length)
                out.extend(xor_b[leading_zeros:])
            prev = r
    return out

def encode_image(original_image):
    h, w, c = original_image.shape
    bs = 8
    pad_h = (bs - h % bs) % bs
    pad_w = (bs - w % bs) % bs
    padded_image = np.pad(original_image, ((0, pad_h), (0, pad_w), (0, 0)), mode='constant', constant_values=0)
    ph, pw, _ = padded_image.shape
    out = bytearray(b'BXC\x00')
    out.extend(h.to_bytes(4, 'big'))
    out.extend(w.to_bytes(4, 'big'))
    out.append(c)
    out.append(bs)
    for by in range(0, ph, bs):
        for bx in range(0, pw, bs):
            block = padded_image[by:by + bs, bx:bx + bs, :]
            out.extend(bxc_encode_block(block))
    return bytes(out)

def bxc_decode_block(comp, pos, channels, block_size):
    block = np.zeros((block_size, block_size, channels), dtype=np.uint8)
    for ch in range(channels):
        first_row_b = comp[pos:pos + block_size]
        pos += block_size
        first_row = int.from_bytes(first_row_b, 'big')
        for x in range(block_size):
            block[0, x, ch] = (first_row >> ((block_size - 1 - x) * 8)) & 0xFF
        prev = first_row
        for y in range(1, block_size):
            len_byte = comp[pos]
            pos += 1
            if len_byte == 0:
                row = prev
            else:
                xor_b = b'\x00' * (block_size - len_byte) + comp[pos:pos + len_byte]
                pos += len_byte
                xor_val = int.from_bytes(xor_b, 'big')
                row = prev ^ xor_val
            for x in range(block_size):
                block[y, x, ch] = (row >> ((block_size - 1 - x) * 8)) & 0xFF
            prev = row
    return block, pos

def decode_image(comp):
    if comp[:4] != b'BXC\x00':
        raise ValueError("Invalid magic")
    pos = 4
    h = int.from_bytes(comp[pos:pos + 4], 'big')
    pos += 4
    w = int.from_bytes(comp[pos:pos + 4], 'big')
    pos += 4
    c = comp[pos]
    pos += 1
    bs = comp[pos]
    pos += 1
    pad_h = (bs - h % bs) % bs
    pad_w = (bs - w % bs) % bs
    ph = h + pad_h
    pw = w + pad_w
    image = np.zeros((ph, pw, c), dtype=np.uint8)
    for by in range(0, ph, bs):
        for bx in range(0, pw, bs):
            block, pos = bxc_decode_block(comp, pos, c, bs)
            image[by:by + bs, bx:bx + bs, :] = block
    return image[:h, :w, :]

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python script.py image1.png [image2.png ...]")
        sys.exit(1)
    for fname in sys.argv[1:]:
        img = imread(fname)
        if img.dtype != np.uint8:
            if img.dtype == np.float32 or img.dtype == np.float64:
                img = (img * 255).astype(np.uint8)
            else:
                raise ValueError(f"Unsupported dtype {img.dtype}")
        if len(img.shape) == 2:
            img = np.stack([img] * 3, axis=2)  # grayscale to RGB
        elif img.shape[2] == 1:
            img = np.repeat(img, 3, axis=2)
        elif img.shape[2] not in (3, 4):
            raise ValueError(f"Unsupported channels {img.shape[2]}")
        comp = encode_image(img)
        original_size = img.nbytes
        compressed_size = len(comp)
        ratio = original_size / compressed_size if compressed_size > 0 else 0
        dec_img = decode_image(comp)
        lossless = np.array_equal(img, dec_img)
        print(f"File: {fname}")
        print(f"Original size: {original_size} bytes")
        print(f"Compressed size: {compressed_size} bytes")
        print(f"Compression ratio: {ratio:.2f}")
        print(f"Lossless round-trip: {lossless}")
        print()