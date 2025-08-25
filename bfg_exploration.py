import numpy as np
import sys

BX3_MAGIC = b'BX3\x00'
OP_RSML      = 0x00  # 00xxxxxx - Residual Small: encodes small color differences (-2 to +1) for RGB channels with no alpha change
OP_RECENT    = 0x60  # 011000xx (xx = 0-3) - Recent Colors: references one of the 4 most recently used colors
OP_INDEX     = 0x80  # 10xxxxxx - Index Cache: references a color from the 64-entry hash-based color cache
OP_ZRLE      = 0xC0  # 110xxxxx, len=low5+1 (1..32) - Zero Run Length Encoding: repeats the predicted color for 1-32 pixels
OP_RAW3      = 0xE0  # 11100000 - Raw RGB: stores uncompressed RGB values (3 bytes)
OP_RAW4      = 0xE1  # 11100001 - Raw RGBA: stores uncompressed RGBA values (4 bytes)
OP_RMED      = 0xE2  # 11100010 - Residual Medium: encodes medium color differences (-8 to +7) for RGB channels with no alpha change
OP_AONLY     = 0xE3  # 11100011 - Alpha Only: stores only the alpha channel value when RGB matches prediction

def _hash_idx(r,g,b,a):  # 64-entry cache
    return ((r*5) ^ (g*7) ^ (b*9) ^ (a*11)) & 63

def predict(filter_type, left, up, leftup):
    if filter_type == 0:  # SUB
        return left
    elif filter_type == 1:  # UP
        return up
    elif filter_type == 2:  # AVG
        return tuple((l + u) // 2 for l, u in zip(left, up))
    elif filter_type == 3:  # PAETH
        def paeth_one(a, b, c):
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
        return tuple(paeth_one(l, u, lu) for l, u, lu in zip(left, up, leftup))

def update_recent(recent, color):
    if color in recent:
        recent.remove(color)
    recent.insert(0, color)
    if len(recent) > 4:
        recent.pop()

def bx3_encode(img: np.ndarray) -> bytes:
    h, w, c = img.shape
    assert c in (3,4)
    out = bytearray(BX3_MAGIC)
    out += h.to_bytes(4,'big') + w.to_bytes(4,'big') + bytes([c])

    cache = [(0,0,0,0)]*64
    recent = []
    prev = (0,0,0,255)
    up_row = [(0,0,0,255)]*w

    import math
    inf = math.inf

    for y in range(h):
        best_size = inf
        best_ft = None
        best_out = None
        best_next = None
        best_cache = None
        best_recent = None
        best_prev = None

        for ft in range(4):
            temp_out = bytearray()
            temp_cache = cache[:]
            temp_recent = recent[:]
            temp_prev = (0,0,0,255)  # reset per row for prediction
            temp_run = 0
            temp_next = [None]*w

            for x in range(w):
                px = img[y, x]
                if c == 3:
                    r,g,b = map(int, px)
                    a = 255
                else:
                    r,g,b,a = map(int, px)
                cur = (r,g,b,a)

                left = temp_prev
                up = up_row[x]
                leftup = up_row[x-1] if x > 0 else (0,0,0,255)
                predicted = predict(ft, left, up, leftup)

                if cur == predicted:
                    temp_run += 1
                    if temp_run == 32:
                        temp_out.append(OP_ZRLE | 31)
                        temp_run = 0
                    temp_next[x] = cur
                    temp_prev = cur
                    update_recent(temp_recent, cur)
                    continue

                if temp_run > 0:
                    temp_out.append(OP_ZRLE | (temp_run - 1))
                    temp_run = 0

                # Check recent
                found = False
                for i in range(len(temp_recent)):
                    if temp_recent[i] == cur:
                        temp_out.append(OP_RECENT | i)
                        del temp_recent[i]
                        temp_recent.insert(0, cur)
                        temp_next[x] = cur
                        temp_prev = cur
                        found = True
                        break
                if found:
                    continue

                # Check index
                idx = _hash_idx(r,g,b,a)
                if temp_cache[idx] == cur:
                    temp_out.append(OP_INDEX | idx)
                    temp_recent.insert(0, cur)
                    if len(temp_recent) > 4:
                        temp_recent.pop()
                    temp_next[x] = cur
                    temp_prev = cur
                    continue

                # Compute residuals
                dr = r - predicted[0]
                dg = g - predicted[1]
                db = b - predicted[2]
                da = a - predicted[3]

                # RSML
                if da == 0 and all(-2 <= d <= 1 for d in (dr, dg, db)):
                    code = ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2)
                    temp_out.append(OP_RSML | code)
                    temp_cache[idx] = cur
                    temp_recent.insert(0, cur)
                    if len(temp_recent) > 4:
                        temp_recent.pop()
                    temp_next[x] = cur
                    temp_prev = cur
                    continue

                # AONLY
                if c == 4 and dr == dg == db == 0:
                    temp_out.append(OP_AONLY)
                    temp_out.append(a)
                    temp_cache[idx] = cur
                    temp_recent.insert(0, cur)
                    if len(temp_recent) > 4:
                        temp_recent.pop()
                    temp_next[x] = cur
                    temp_prev = cur
                    continue

                # RMED
                if da == 0 and all(-8 <= d <= 7 for d in (dr, dg, db)):
                    temp_out.append(OP_RMED)
                    d1 = ((dr + 8) << 4) | (dg + 8)
                    d2 = ((db + 8) << 4) | 0
                    temp_out += bytes([d1, d2])
                    temp_cache[idx] = cur
                    temp_recent.insert(0, cur)
                    if len(temp_recent) > 4:
                        temp_recent.pop()
                    temp_next[x] = cur
                    temp_prev = cur
                    continue

                # RAW
                if c == 3:
                    temp_out.append(OP_RAW3)
                    temp_out += bytes([r, g, b])
                else:
                    temp_out.append(OP_RAW4)
                    temp_out += bytes([r, g, b, a])
                temp_cache[idx] = cur
                temp_recent.insert(0, cur)
                if len(temp_recent) > 4:
                    temp_recent.pop()
                temp_next[x] = cur
                temp_prev = cur

            if temp_run > 0:
                temp_out.append(OP_ZRLE | (temp_run - 1))

            size = len(temp_out)
            if size < best_size:
                best_size = size
                best_ft = ft
                best_out = temp_out
                best_next = temp_next
                best_cache = temp_cache[:]
                best_recent = temp_recent[:]
                best_prev = temp_prev

        # Apply best
        out.append(best_ft)
        out += best_out
        up_row = best_next
        cache = best_cache
        recent = best_recent
        prev = best_prev

    return bytes(out)

def bx3_decode(data: bytes) -> np.ndarray:
    if data[:4] != BX3_MAGIC:
        raise ValueError("bad magic")
    pos = 4
    h = int.from_bytes(data[pos:pos+4],'big'); pos += 4
    w = int.from_bytes(data[pos:pos+4],'big'); pos += 4
    c = data[pos]; pos += 1
    if c not in (3,4):
        raise ValueError("unsupported channel count")
    out = np.zeros((h,w,c), dtype=np.uint8)

    cache = [(0,0,0,0)]*64
    recent = []
    prev = (0,0,0,255)
    up_row = [(0,0,0,255)]*w

    x = y = 0

    def rebuild_up_row():
        nonlocal up_row
        if y > 0:
            row = out[y-1]
            if c == 3:
                up_row = [(int(px[0]), int(px[1]), int(px[2]), 255) for px in row]
            else:
                up_row = [(int(px[0]), int(px[1]), int(px[2]), int(px[3])) for px in row]

    def emit(r, g, b, a):
        nonlocal x, y, prev
        if c == 3:
            out[y, x] = (r, g, b)
        else:
            out[y, x] = (r, g, b, a)
        prev = (r, g, b, a)
        x += 1
        if x == w:
            x = 0
            y += 1
            if y < h:
                rebuild_up_row()

    filter_type = 0  # dummy init

    while y < h:
        if x == 0:
            filter_type = data[pos]
            pos += 1
            if filter_type > 3:
                raise ValueError("invalid filter type")

        left = (0, 0, 0, 255) if x == 0 else prev
        up = up_row[x] if y > 0 else (0, 0, 0, 255)
        leftup = (0, 0, 0, 255) if x == 0 else up_row[x - 1]
        predicted = predict(filter_type, left, up, leftup)
        pr, pg, pb, pa = predicted

        b0 = data[pos]
        pos += 1

        if b0 & 0xE0 == 0xC0:  # ZRLE
            run = (b0 & 0x1F) + 1
            for k in range(run):
                if k > 0:
                    left = (0, 0, 0, 255) if x == 0 else prev
                    up = up_row[x] if y > 0 else (0, 0, 0, 255)
                    leftup = (0, 0, 0, 255) if x == 0 else up_row[x - 1]
                    predicted = predict(filter_type, left, up, leftup)
                r, g, b, a = predicted
                emit(r, g, b, a)
                update_recent(recent, (r, g, b, a))
        elif 0x60 <= b0 <= 0x63:  # RECENT
            i = b0 & 0x03
            if len(recent) <= i:
                raise ValueError("invalid recent index")
            r, g, b, a = recent[i]
            emit(r, g, b, a)
            del recent[i]
            recent.insert(0, (r, g, b, a))
        elif b0 & 0xC0 == 0x80:  # INDEX
            idx = b0 & 0x3F
            r, g, b, a = cache[idx]
            emit(r, g, b, a)
            update_recent(recent, (r, g, b, a))
        elif b0 == OP_RAW3:
            r = data[pos]
            g = data[pos + 1]
            b = data[pos + 2]
            pos += 3
            a = 255
            idx = _hash_idx(r, g, b, a)
            cache[idx] = (r, g, b, a)
            emit(r, g, b, a)
            update_recent(recent, (r, g, b, a))
        elif b0 == OP_RAW4:
            r = data[pos]
            g = data[pos + 1]
            b = data[pos + 2]
            a = data[pos + 3]
            pos += 4
            idx = _hash_idx(r, g, b, a)
            cache[idx] = (r, g, b, a)
            emit(r, g, b, a)
            update_recent(recent, (r, g, b, a))
        elif b0 == OP_RMED:
            d1 = data[pos]
            pos += 1
            d2 = data[pos]
            pos += 1
            dr = (d1 >> 4) - 8
            dg = (d1 & 0x0F) - 8
            db = (d2 >> 4) - 8
            r = (pr + dr) & 0xFF
            g = (pg + dg) & 0xFF
            b = (pb + db) & 0xFF
            a = pa
            idx = _hash_idx(r, g, b, a)
            cache[idx] = (r, g, b, a)
            emit(r, g, b, a)
            update_recent(recent, (r, g, b, a))
        elif b0 == OP_AONLY:
            a = data[pos]
            pos += 1
            r, g, b = pr, pg, pb
            idx = _hash_idx(r, g, b, a)
            cache[idx] = (r, g, b, a)
            emit(r, g, b, a)
            update_recent(recent, (r, g, b, a))
        elif b0 < 0x40:  # RSML
            code = b0 & 0x3F
            dr = (code >> 4) - 2
            dg = ((code >> 2) & 0x03) - 2
            db = (code & 0x03) - 2
            r = (pr + dr) & 0xFF
            g = (pg + dg) & 0xFF
            b = (pb + db) & 0xFF
            a = pa
            idx = _hash_idx(r, g, b, a)
            cache[idx] = (r, g, b, a)
            emit(r, g, b, a)
            update_recent(recent, (r, g, b, a))
        else:
            raise ValueError(f"unknown opcode {hex(b0)}")

    return out

def main():
    from matplotlib.image import imread
    from PIL import Image
    import io
    import png
    import signal

    if len(sys.argv) < 2:
        print("Usage: python bx3.py image1.png [image2.png ...]")
        sys.exit(1)

    # Counters for compression statistics
    better_compression_count = 0
    worse_or_equal_compression_count = 0
    
    def print_statistics():
        total = better_compression_count + worse_or_equal_compression_count
        if total > 0:
            print("\n" + "="*50)
            print("COMPRESSION STATISTICS:")
            print(f"Images with BX3 ratio < max(libpng, pypng): {better_compression_count}")
            print(f"Images with BX3 ratio >= max(libpng, pypng): {worse_or_equal_compression_count}")
            print(f"Total images processed: {total}")
            print(f"Percentage with better compression: {(better_compression_count/total)*100:.1f}%")
            print("="*50)
    
    def signal_handler(sig, frame):
        print("\nInterrupted by user (Ctrl+C)")
        print_statistics()
        sys.exit(0)
    
    # Set up signal handler for Ctrl+C
    signal.signal(signal.SIGINT, signal_handler)

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

        # Encode with BX3
        comp = bx3_encode(img)
        original_size = img.nbytes
        compressed_size = len(comp)
        ratio = compressed_size / original_size if original_size > 0 else 0

        # Compare with libpng (via PIL)
        pil_img = Image.fromarray(img)
        png_buffer = io.BytesIO()
        pil_img.save(png_buffer, format='PNG', optimize=True)
        png_compressed = png_buffer.getvalue()
        png_size = len(png_compressed)
        png_ratio = png_size / original_size if original_size > 0 else 0

        # Compare with pypng (pure Python)
        pypng_buffer = io.BytesIO()
        h, w, c = img.shape
        if c == 3:
            png_writer = png.Writer(width=w, height=h, greyscale=False, alpha=False)
            img_2d = img.reshape(h, w*3)
        else:
            png_writer = png.Writer(width=w, height=h, greyscale=False, alpha=True)
            img_2d = img.reshape(h, w*4)
        png_writer.write(pypng_buffer, img_2d)
        pypng_compressed = pypng_buffer.getvalue()
        pypng_size = len(pypng_compressed)
        pypng_ratio = pypng_size / original_size if original_size > 0 else 0

        # Compare BX3 compression with max of PNG implementations
        max_png_ratio = max(png_ratio, pypng_ratio)
        if ratio < max_png_ratio:
            better_compression_count += 1
        else:
            worse_or_equal_compression_count += 1

        # Print results
        print(f"File: {fname} | {original_size} bytes -> {compressed_size} bytes")
        print(f"  this:    {ratio:.3f}")
        print(f"  libpng:  {png_ratio:.3f}")
        print(f"  pypng:   {pypng_ratio:.3f}")

        # Verify round trip
        dec_img = bx3_decode(comp)
        lossless = np.array_equal(img, dec_img)
        assert lossless, "Lossless round-trip failed"
        print()

    # Print final statistics after processing all images
    print_statistics()


if __name__ == "__main__":
    main()