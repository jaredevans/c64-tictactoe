# C64 Tic-Tac-Toe

Two-player tic-tac-toe for the Commodore 64, written in C with [cc65](https://cc65.github.io/) and rendered directly to the VIC-II hi-res bitmap.

![Game in progress](ttt.png)

## Controls

Cells are numbered 1–9 in numpad layout:

```
 1 | 2 | 3
---+---+---
 4 | 5 | 6
---+---+---
 7 | 8 | 9
```

Players alternate (X then O). Press the digit for an empty cell to place your mark. After a win or draw, press any key to start a new game.

## Build & run

Requires `cl65` (cc65) and `x64sc` (VICE). The Makefile points at a local VICE install — adjust `EMU` if yours lives elsewhere.

```sh
make            # builds ttt.prg
make run        # launches it in x64sc
```

## Tests

`check_winner` and related board logic are exercised by a host-side unit test that compiles the same `ttt.c` with `-DUNIT_TEST`:

```sh
make test
```

## How it works

### Video mode

`init_video()` puts the VIC-II into standard hi-res bitmap mode (320×200, 1 bit per pixel):

| Register | Value | Meaning |
|---|---|---|
| `$D011` CR1 | `$3B` | display enable, 25 rows, BMM=1 |
| `$D016` CR2 | `$C8` | 40 columns, no multicolor |
| `$D018` MEMPTR | `$18` | screen RAM at `$0400`, bitmap at `$2000` |
| `$D020/$D021` | `$06` | border + background = blue |

The bitmap (8000 bytes at `$2000`) holds the pixels; screen RAM (1000 bytes at `$0400`) holds a per-character-cell color attribute (high nibble = foreground, low nibble = background).

### Bitmap addressing

The screen is logically a 25×40 grid of 8×8 character cells. Within a cell, the 8 pixel rows are stored as 8 consecutive bytes, MSB-first. So pixel `(x, y)` maps to:

```
byte = BITMAP + (y >> 3) * 320 + (x & ~7) + (y & 7)
bit  = 0x80 >> (x & 7)
```

That's the formula `plot()` implements. `fill_rect()` is the fast path for the big repaints (clearing a cell, wiping the board between games): it walks one *byte-column* at a time, using partial-byte masks at the left and right edges and plain byte stores (`0xFF`/`0x00`) in the middle, skipping the address math `plot()` would do per pixel.

### Color

Hi-res bitmap mode is one bitplane, so X (light blue) and O (yellow) can't both glow on the same character cell. `set_cell_color()` recolors every 8×8 char cell that overlaps the mark's 40×40 area by writing `(fg << 4) | bg` into the corresponding bytes of screen RAM. The grid lines stay white because their character cells aren't touched.

### Mark glyphs

Each cell carries a centered 40×40 mark:

- **X** — four overlapping diagonals, each offset by 1 px, giving a 4-px-thick stroke without anti-aliasing.
- **O** — Bresenham midpoint circle (`draw_circle`) called at four consecutive radii (`r-1..r-4`), producing a 4-px-thick ring with 8-way symmetry per `plot_circle_points`.

Before drawing, `clear_cell_mark()` wipes the entire cell rectangle so cell-color changes don't leave the digit hint visible in the new color.

### Text

A small built-in 5×7 font (`FONT[]`) covers just the characters used by the UI (digits, dash, and the letters in `TIC-TAC-TOE`, `PLAYER X/O`, `X WINS`, `O WINS`, `DRAW`, `PRESS ANY KEY`). Each glyph is 7 bytes — one byte per pixel row, top 5 bits used. `draw_text()` walks the string at 6 px per character (5 px glyph + 1 px gap).

### Game loop

`main()` is a tight outer loop over games and an inner loop over turns:

```c
for (;;) {                       // one game per iteration
    reset_board(); clear_board_area(); draw_grid(); draw_all_hints();
    while (!winner) {
        draw_status("PLAYER X" or "PLAYER O", ...);
        play_turn(player);       // blocks on cgetc()
        winner = check_winner();
        if (!winner) player = (player == 'X') ? 'O' : 'X';
    }
    show_endgame(winner);
    cgetc();                     // wait for any key, then new game
}
```

`play_turn()` uses cc65's blocking `cgetc()` — no IRQ handling, no raster sync. The C64 spends almost all its time idle in `cgetc()`; the screen stays static between keystrokes, which is exactly what we want.

Input is the digits `1`–`9` in numpad layout. The handler ignores any other key, and ignores digits whose cell is already occupied.

`check_winner()` walks the 8 winning lines table and returns `'X'`, `'O'`, `'D'` (draw — board is full), or `0` (game continues).

### Host-side tests

`test_winner.c` `#include`s `ttt.c` with `-DUNIT_TEST` defined. That sentinel guards everything in `ttt.c` that depends on the C64 environment (`#include <conio.h>`, `main()`, the VIC-II pokes), so the file compiles as a plain library on the host. The test then drives `check_winner()` against fixed board layouts with a simple `expect(name, "XOX.OX.X.", 'X')` helper. This means the win-detection logic is the same code on both targets — no duplication, no risk of divergence.
