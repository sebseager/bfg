import sys
from PIL import Image


def get_pixels(path):
    im = Image.open(path, "r")
    return im.getdata()


def print_chans(pixels):
    channels = zip(*pixels)
    for i, chan in enumerate(channels):
        print(f"-- CHAN {i} --")
        for j, p in enumerate(chan):
            print(f"PX {j}:\t{p}")


# print frequencies of pixels across all channels, sorted highest to lowest
def print_freqs(pixels):
    channels = zip(*pixels)
    for i, chan in enumerate(channels):
        print(f"-- CHAN {i} --")
        freqs = {}
        for p in chan:
            if p in freqs:
                freqs[p] += 1
            else:
                freqs[p] = 1
        sorted_freqs = sorted(freqs.items(), key=lambda x: x[1], reverse=True)
        for k, v in sorted_freqs:
            print(f"{k}:\t{v}")


if __name__ == "__main__":
    pixels = get_pixels(sys.argv[1])
    # print_chans(pixels)
    print_freqs(pixels)
