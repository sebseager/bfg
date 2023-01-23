# BFG – Block-Based Fast Graphics

An experiment with fast lossless image conversion.

## Usage

For ease of portability, BFG was written as a small C library (`bfg.c` and `bfg.h`). An example program, `evaluate.c` is provided, which demonstrates conversion to and from the PNG format and reports some basic statistics on the conversion.

To run `evaluate.c`, first compile everything together, then provide PNG files as arguments. Note that an `output/` directory will be made in the program's directory, and converted images will be placed there.

```bash
make all
./evaluate <png files>
```

## Warnings

This is experimental code and has not been rigorously tested.

## Authors

[Seb Seager](https://github.com/sebseager)

## License

This library is available under the MIT License. See the `LICENSE` file for details.
