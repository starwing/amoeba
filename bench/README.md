# bench

Four self-contained benchmark programs.  Each defines
`AM_IMPLEMENTATION` in its own translation unit, so build each one as
its own executable; never link two of them into one program.

| file | scene | engine | run |
|---|---|---|---|
| `enaml_like_benchmark.cpp` | enaml-like list | amoeba | `just bench-enaml` |
| `yoga_benchmark.cpp` | the same | Yoga | `just bench-enaml` |
| `gui_layout_benchmark.c` | DSH window (272 boxes) | amoeba | `just bench` |
| `yoga_gui_benchmark.cpp` | the same | Yoga | `just bench-yoga` |

Support files: `gui_layout_scene.h` is the shared GUI scene description,
`yogaone.cpp` compiles Yoga from source for both Yoga programs, and
`nanobench.h` is the vendored timer used by the historical pair.

## Running

```sh
just bench-enaml        # both old programs, amoeba then Yoga
just bench              # GUI scene in amoeba
just bench-yoga         # GUI scene in Yoga
just layout 2748 1263   # dump the GUI boxes
```

Yoga is compiled from the source tree in `YOGA_SRC` (default
`~/Work/Sources/yoga`).  `just bench` and `just bench-yoga` build with
`-O2 -DNDEBUG` into `tests/build/`; `just layout` writes the box dump
read by `misc/render_layout.py`.

The GUI pair reports build / suggest / dump / load with a small C
timer.  The historical pair uses nanobench and prints its own table.
Numbers depend on the machine; the repository README has the current
GUI comparison.
