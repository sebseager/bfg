import sys
from pathlib import Path
import numpy as np
from os import stat
from PIL import Image
from collections import Counter
import pdb

BITS_PER_CHAN = 8
N_CHANNELS = 3


# read PNG from path and return RGB numpy array
def read_img(path):
    path = Path(path)
    img = Image.open(path)
    arr = np.array(img, dtype=int)[:, :, :3]
    return arr


def linear_idx(row, col, arr):
    return row * arr.shape[1] + col


def np_idx(linear_idx, arr):
    return linear_idx // arr.shape[1], linear_idx % arr.shape[1]


# compress whole RGB image (no alpha) using only run-length encoding
def rle_test(arr, rle_bits=4):
    pass


# compress whole RGB image (no alpha) using only n-bit lookback
def offset_only_test(arr, lookback_bits=2):
    header_bits = N_CHANNELS
    rows, cols = arr.shape[:2]

    chan_names = {0: "r", 1: "g", 2: "b"}
    stats = {
        "n_chans_replaced": Counter(),
        "r_lookback_deltas": Counter(),
        "g_lookback_deltas": Counter(),
        "b_lookback_deltas": Counter(),
    }

    lookback_pixels = 2**lookback_bits

    # perform encoding
    for idx in range(linear_idx(rows - 1, cols - 1, arr)):
        current = arr[(*np_idx(idx, arr),)]
        lookback_stop = idx - lookback_pixels - 1 if idx > lookback_pixels else 0
        chans_replaced = [False, False, False]
        for i in range(idx - 1, lookback_stop, -1):
            r, c = np_idx(i, arr)
            for chan in range(N_CHANNELS):
                if chans_replaced[chan]:
                    continue
                if arr[r, c, chan] == current[chan]:
                    delta = i - idx
                    stats[f"{chan_names[chan]}_lookback_deltas"][delta] += 1
                    chans_replaced[chan] = True
            if all(chans_replaced):
                break

        stats["n_chans_replaced"][sum(chans_replaced)] += 1

    print("OFFSET ONLY")
    # print("total pixels", rows * cols)
    # print("sanity check", sum(stats["n_chans_replaced"].values()))
    # print(
    #     "fraction pixels compressed",
    #     (rows * cols - stats["n_chans_replaced"][0]) / (rows * cols),
    # )
    print(
        "compression ratio",
        sum(
            stats["n_chans_replaced"][i]
            * (BITS_PER_CHAN * (N_CHANNELS - i) + lookback_bits * i)
            for i in range(4)
        )
        / (rows * cols * 24),
    )


# compress whole RGB image (no alpha) using
# either n-bit lookback or a 2-bit difference from the previous value
# (one bit stores sign)
def offset_or_diff_test(arr, lookback_bits=3):
    header_bits = N_CHANNELS * 2
    lookback_pixels = 2**lookback_bits
    max_diff = 2**lookback_bits
    rows, cols = arr.shape[:2]

    n_chans_replaced = Counter()

    # perform encoding
    for idx in range(linear_idx(rows - 1, cols - 1, arr)):
        current = arr[(*np_idx(idx, arr),)]
        lookback_stop = idx - lookback_pixels - 1 if idx > lookback_pixels else 0
        chans_replaced = [False] * N_CHANNELS
        for i in range(idx - 1, lookback_stop, -1):
            r, c = np_idx(i, arr)
            for chan in range(N_CHANNELS):
                if chans_replaced[chan]:
                    continue
                diff = current[chan] - arr[r, c, chan]
                if abs(diff) <= max_diff:
                    chans_replaced[chan] = True
                if arr[r, c, chan] == current[chan]:
                    delta = i - idx
                    chans_replaced[chan] = True
            if all(chans_replaced):
                break

        n_chans_replaced[sum(chans_replaced)] += 1

    print("OFFSET OR DIFF")
    # print("total pixels", rows * cols)
    # print("sanity check", sum(n_chans_replaced.values()))
    # print(
    #     "fraction pixels compressed",
    #     (rows * cols - n_chans_replaced[0]) / (rows * cols),
    # )
    print(
        "compression ratio",
        sum(
            n_chans_replaced[i]
            * (header_bits + BITS_PER_CHAN * (N_CHANNELS - i) + lookback_bits * i)
            for i in range(N_CHANNELS + 1)
        )
        / (rows * cols * (BITS_PER_CHAN + header_bits) * N_CHANNELS),
    )


# compress whole RGB image (no alpha) using difference block
def diff_block_test(arr, diff_bits=4):
    rows, cols = arr.shape[:2]
    total_pixels = rows * cols
    max_diff = 2**diff_bits + 1
    min_diff = -(2**diff_bits)

    full_val_width = BITS_PER_CHAN + 1
    block_start_width = 1
    block_stop_width = 2
    diff_width = diff_bits + 2  # one to start, one for sign
    same_val_width = 2  # constant
    worst_chan_cost = total_pixels * full_val_width

    chan_costs = [0] * N_CHANNELS
    n_same_vals = [0] * N_CHANNELS
    n_diff_vals = [0] * N_CHANNELS
    n_raw_vals = [0] * N_CHANNELS

    for chan in range(N_CHANNELS):
        block_val = None
        block_active = False
        for idx in range(linear_idx(rows - 1, cols - 1, arr)):
            r, c = np_idx(idx, arr)
            current = arr[r, c, chan]
            if block_val is None:
                block_val = current
                chan_costs[chan] += full_val_width
                n_raw_vals[chan] += 1
            elif block_active:
                diff = current - block_val
                if diff == 0:
                    chan_costs[chan] += same_val_width
                    n_same_vals[chan] += 1
                elif diff <= max_diff and diff >= min_diff:
                    chan_costs[chan] += diff_width
                    n_diff_vals[chan] += 1
                else:
                    chan_costs[chan] += block_stop_width + full_val_width
                    n_raw_vals[chan] += 1
                    block_val = current
                    block_active = False
            else:
                diff = current - block_val
                if diff == 0:
                    chan_costs[chan] += block_start_width + same_val_width
                    n_same_vals[chan] += 1
                    block_active = True
                elif diff <= max_diff and diff >= min_diff:
                    chan_costs[chan] += block_start_width + diff_width
                    n_diff_vals[chan] += 1
                    block_active = True
                else:
                    block_val = current
                    chan_costs[chan] += full_val_width

    print("DIFF BLOCK")
    # print("total pixels", rows * cols)
    # print("worst channel cost", worst_chan_cost)
    # print("chan costs", chan_costs)
    # print("n same vals", n_same_vals)
    # print("n diff vals", n_diff_vals)
    # print("n raw vals", n_raw_vals)
    print("compression ratio by channel", [c / worst_chan_cost for c in chan_costs])
    print("compression ratio", sum(chan_costs) / (worst_chan_cost * 3))


def moving_avg_test(arr, diff_bits=3, rle_bits=0):
    # rotate arr right 90 degrees
    # arr = np.rot90(arr, k=1)

    rows, cols = arr.shape[:2]
    total_pixels = rows * cols
    max_diff = 2**diff_bits + 1
    min_diff = -(2**diff_bits)

    # raw value is 00 ########
    # same as moving avg is 01
    # positive diff with moving avg is 10 diff_bits
    # negative diff is 11 diff_bits
    raw_val_width = BITS_PER_CHAN + 2
    diff_width = diff_bits + 2
    rle_width = rle_bits + 2
    worst_chan_cost = total_pixels * raw_val_width
    max_run_len = 2**rle_bits

    chan_costs = [0] * N_CHANNELS
    n_same_vals = [0] * N_CHANNELS
    n_diff_vals = [0] * N_CHANNELS
    n_raw_vals = [0] * N_CHANNELS

    for chan in range(N_CHANNELS):
        avg = 0  # not actually, but something like that
        run_len = 0
        for idx in range(linear_idx(rows - 1, cols - 1, arr)):
            r, c = np_idx(idx, arr)
            current = arr[r, c, chan]
            if idx == 0:
                chan_costs[chan] += raw_val_width
                n_raw_vals[chan] += 1
                avg = current
            else:
                if current == avg:
                    run_len += 1
                    if run_len == 1:
                        chan_costs[chan] += rle_width
                        n_same_vals[chan] += 1
                    elif run_len == max_run_len:
                        run_len = 0
                        chan_costs[chan] += raw_val_width
                        n_raw_vals[chan] += 1
                    else:
                        # we just increment the RLE - no effect on cost
                        pass
                else:
                    diff = current - avg
                    if diff <= max_diff and diff >= min_diff:
                        chan_costs[chan] += diff_width
                        n_diff_vals[chan] += 1
                    else:
                        chan_costs[chan] += raw_val_width
                        n_raw_vals[chan] += 1

            prev_val = current
            avg += (current - avg) // 2

    print("MOVING AVG")
    print("total pixels", rows * cols)
    print("worst channel cost", worst_chan_cost)
    print("chan costs", chan_costs)
    print("n same vals", n_same_vals)
    print("n diff vals", n_diff_vals)
    print("n raw vals", n_raw_vals)
    print("compression ratio by channel", [c / worst_chan_cost for c in chan_costs])
    print("compression ratio", sum(chan_costs) / (worst_chan_cost * 3))


if __name__ == "__main__":
    test = read_img(sys.argv[1])
    # offset_only_test(test)
    # offset_or_diff_test(test)
    # diff_block_test(test)
    moving_avg_test(test)
