import numpy as np
import sys

BX2_MAGIC = b'BX2\x00'
OP_DIFF_LEFT = 0x00  # 00xxxxxx
OP_DIFF_UP   = 0x40  # 01xxxxxx
OP_INDEX     = 0x80  # 10xxxxxx
OP_RUN_BASE  = 0xC0  # 110xxxxx, len=low5+1 (1..32)
OP_RAW3      = 0xE0  # 11100000
OP_RAW4      = 0xE1  # 11100001

def _hash_idx(r,g,b,a):  # 64-entry cache, distinct from QOI’s hash
    return ( (r*5) ^ (g*7) ^ (b*9) ^ (a*11) ) & 63

def bx2_encode(img: np.ndarray) -> bytes:
    h, w, c = img.shape
    assert c in (3,4)
    out = bytearray(BX2_MAGIC)
    out += h.to_bytes(4,'big') + w.to_bytes(4,'big') + bytes([c])

    cache = [(0,0,0,0)]*64
    run = 0
    prev = (0,0,0,255) if c==4 else (0,0,0,255)  # alpha virtual=255 for RGB
    up_row = [(0,0,0,255)]*w

    for y in range(h):
        # swap in new up_row; build next_up as current row as we go
        next_up = [None]*w
        for x in range(w):
            px = img[y, x]
            if c == 3:
                r,g,b = int(px[0]), int(px[1]), int(px[2])
                a = 255
            else:
                r,g,b,a = map(int, px)

            cur = (r,g,b,a)

            # RUN (relative to prev pixel)
            if cur == prev:
                run += 1
                if run == 32:
                    out.append(OP_RUN_BASE | (32-1))
                    run = 0
                next_up[x] = cur
                prev = cur
                continue
            if run:
                out.append(OP_RUN_BASE | (run-1))
                run = 0

            # INDEX
            idx = _hash_idx(r,g,b,a)
            if cache[idx] == cur:
                out.append(OP_INDEX | idx)
                next_up[x] = cur
                prev = cur
                continue

            # DIFF-LEFT-SMALL
            pr,pg,pb,pa = prev
            dr, dg, db = r-pr, g-pg, b-pb
            if (-1 <= dr <= 2) and (-1 <= dg <= 2) and (-1 <= db <= 2):
                code = ((dr+1)<<4) | ((dg+1)<<2) | (db+1)
                out.append(OP_DIFF_LEFT | code)
                cache[idx] = cur
                next_up[x] = cur
                prev = cur
                continue

            # DIFF-UP-SMALL (only if y>0)
            ur,ug,ub,ua = up_row[x] if y>0 else (0,0,0,255)
            dr, dg, db = r-ur, g-ug, b-ub
            if y>0 and (-1 <= dr <= 2) and (-1 <= dg <= 2) and (-1 <= db <= 2):
                code = ((dr+1)<<4) | ((dg+1)<<2) | (db+1)
                out.append(OP_DIFF_UP | code)
                cache[idx] = cur
                next_up[x] = cur
                prev = cur
                continue

            # RAW
            if c == 3:
                out.append(OP_RAW3)
                out += bytes((r,g,b))
            else:
                out.append(OP_RAW4)
                out += bytes((r,g,b,a))

            cache[idx] = cur
            next_up[x] = cur
            prev = cur

        up_row = next_up

    if run:
        out.append(OP_RUN_BASE | (run-1))

    return bytes(out)

def bx2_decode(data: bytes) -> np.ndarray:
    if data[:4] != BX2_MAGIC:
        raise ValueError("bad magic")
    pos = 4
    h = int.from_bytes(data[pos:pos+4],'big'); pos += 4
    w = int.from_bytes(data[pos:pos+4],'big'); pos += 4
    c = data[pos]; pos += 1
    out = np.zeros((h,w,c), dtype=np.uint8)

    cache = [(0,0,0,0)]*64
    prev = (0,0,0,255)
    up_row = [(0,0,0,255)]*w

    x = y = 0
    def emit(r,g,b,a):
        nonlocal x,y,prev
        if c==3:
            out[y,x] = (r,g,b)
        else:
            out[y,x] = (r,g,b,a)
        prev = (r,g,b,a)
        x += 1
        if x == w:
            x = 0
            y += 1

    while y < h:
        b0 = data[pos]; pos += 1
        t = b0 & 0xC0
        if t == 0xC0:  # RUN (110xxxxx) or RAW (>=0xE0)
            if b0 >= 0xE0:  # RAW3/RAW4
                if b0 == OP_RAW3:
                    r,g,b = data[pos], data[pos+1], data[pos+2]; pos += 3
                    a = 255
                    idx = _hash_idx(r,g,b,a)
                    cache[idx] = (r,g,b,a)
                    emit(r,g,b,a)
                elif b0 == OP_RAW4:
                    r,g,b,a = data[pos], data[pos+1], data[pos+2], data[pos+3]; pos += 4
                    idx = _hash_idx(r,g,b,a)
                    cache[idx] = (r,g,b,a)
                    emit(r,g,b,a)
                else:
                    raise ValueError("reserved RAW opcode")
            else:
                run = (b0 & 0x1F) + 1
                for _ in range(run):
                    r,g,b,a = prev
                    emit(r,g,b,a)
        elif t == 0x80:  # INDEX
            idx = b0 & 0x3F
            r,g,b,a = cache[idx]
            emit(r,g,b,a)
        elif t == 0x00:  # DIFF-LEFT-SMALL
            dr = ((b0>>4)&0x3) - 1
            dg = ((b0>>2)&0x3) - 1
            db = (b0&0x3) - 1
            pr,pg,pb,pa = prev
            r = (pr + dr) & 0xFF
            g = (pg + dg) & 0xFF
            b = (pb + db) & 0xFF
            a = prev[3]
            idx = _hash_idx(r,g,b,a)
            cache[idx] = (r,g,b,a)
            emit(r,g,b,a)
        elif t == 0x40:  # DIFF-UP-SMALL
            dr = ((b0>>4)&0x3) - 1
            dg = ((b0>>2)&0x3) - 1
            db = (b0&0x3) - 1
            up = up_row[x]
            ur,ug,ub,ua = up
            r = (ur + dr) & 0xFF
            g = (ug + dg) & 0xFF
            b = (ub + db) & 0xFF
            a = ua if c==4 else 255
            idx = _hash_idx(r,g,b,a)
            cache[idx] = (r,g,b,a)
            emit(r,g,b,a)
        else:
            raise ValueError("unknown opcode")

        # maintain up_row
        if y < h:
            if x == 0:
                # new row just emitted, rebuild up_row from previous out row
                if y == 0:
                    up_row = [(0,0,0,255)]*w
                else:
                    row = out[y-1] if c==3 else out[y-1]
                    up_row = [ (int(px[0]), int(px[1]), int(px[2]), (int(px[3]) if c==4 else 255)) for px in row ]

    return out

if __name__ == "__main__":
    import sys
    from matplotlib.image import imread
    from PIL import Image
    import io

    if len(sys.argv) < 2:
        print("Usage: python bx2.py image1.png [image2.png ...]")
        sys.exit(1)

    for fname in sys.argv[1:]:
        img = imread(fname)
        if img.dtype != np.uint8:
            if img.dtype in (np.float32, np.float64):
                img = (img * 255).astype(np.uint8)
            else:
                raise ValueError(f"Unsupported dtype {img.dtype}")

        # Normalize to RGB(A)
        if len(img.shape) == 2:
            img = np.stack([img]*3, axis=2)  # grayscale → RGB
        elif img.shape[2] == 1:
            img = np.repeat(img, 3, axis=2)
        elif img.shape[2] not in (3,4):
            raise ValueError(f"Unsupported channels {img.shape[2]}")

        # Encode with BX2
        comp = bx2_encode(img)
        original_size = img.nbytes
        compressed_size = len(comp)
        ratio = compressed_size / original_size if original_size > 0 else 0

        # Compare with libpng
        pil_img = Image.fromarray(img)
        png_buffer = io.BytesIO()
        pil_img.save(png_buffer, format='PNG', optimize=True)
        png_compressed = png_buffer.getvalue()
        png_size = len(png_compressed)
        png_ratio = png_size / original_size if original_size > 0 else 0

        # Print results
        print(f"File: {fname} | {original_size} bytes -> {compressed_size} bytes | "
              f"{ratio:.2f} vs {png_ratio:.2f} (PNG)")

        # Verify round trip
        dec_img = bx2_decode(comp)
        lossless = np.array_equal(img, dec_img)
        assert lossless, "Lossless round-trip failed"
        print()