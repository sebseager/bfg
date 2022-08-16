import sys
from PIL import Image


def get_pixels(path):
    im = Image.open(path, "r")
    return im.getdata()


if __name__ == "__main__":
    pixels = get_pixels(sys.argv[1])
    channels = zip(*pixels)
    for i, chan in enumerate(channels):
        print(f"-- CHAN {i} --")
        for j, p in enumerate(chan):
            print(f"{j}:\t{p}")
