/*
 * ttt.c - C64 tic-tac-toe (two human players)
 * Build:  cl65 -t c64 -O ttt.c -o ttt.prg
 * Run:    x64sc ttt.prg
 */

#include <string.h>
#ifndef UNIT_TEST
#include <conio.h>
#endif

/* ---------- VIC-II registers ---------- */
#define VIC_CR1     (*(unsigned char *)0xD011)
#define VIC_CR2     (*(unsigned char *)0xD016)
#define VIC_MEMPTR  (*(unsigned char *)0xD018)
#define VIC_BORDER  (*(unsigned char *)0xD020)
#define VIC_BG      (*(unsigned char *)0xD021)

/* ---------- Memory layout ---------- */
#define SCREEN_RAM  ((unsigned char *)0x0400)   /* 1000 bytes: color attrs */
#define BITMAP      ((unsigned char *)0x2000)   /* 8000 bytes: hi-res bitmap */
#define BITMAP_LEN  8000U

/* ---------- Colors (C64 palette) ---------- */
#define COL_BLACK      0x00
#define COL_WHITE      0x01
#define COL_BLUE       0x06
#define COL_YELLOW     0x07
#define COL_LIGHTBLUE  0x0E

/* ---------- Board state ---------- */
static unsigned char board[9];      /* 0 = empty, 'X' or 'O' = marked */

static const unsigned char WINNING_LINES[8][3] = {
    {0,1,2}, {3,4,5}, {6,7,8},      /* rows */
    {0,3,6}, {1,4,7}, {2,5,8},      /* cols */
    {0,4,8}, {2,4,6}                /* diagonals */
};

/* Returns 'X', 'O', 'D' (draw) or 0 (game continues). */
static unsigned char check_winner(void) {
    unsigned char i, full = 1, a, b, c;
    for (i = 0; i < 8; i++) {
        a = board[WINNING_LINES[i][0]];
        b = board[WINNING_LINES[i][1]];
        c = board[WINNING_LINES[i][2]];
        if (a != 0 && a == b && a == c) return a;
    }
    for (i = 0; i < 9; i++) if (board[i] == 0) { full = 0; break; }
    return full ? 'D' : 0;
}

static void reset_board(void) {
    unsigned char i;
    for (i = 0; i < 9; i++) board[i] = 0;
}

/* ---------- Board geometry (pixel coords, 320x200) ---------- */
/* Grid box: x = 24..295 (272 wide), y = 24..174 (150 tall).
 * 3 cells of 90 px wide (last is 92 to use the spare).
 * 3 cells of 50 px tall. */
#define GRID_X0    24
#define GRID_X1    295
#define GRID_Y0    24
#define GRID_Y1    174

/* Vertical dividers at x = 114 and x = 205. Horizontal at y = 74 and y = 124. */
#define GRID_VX1   114
#define GRID_VX2   205
#define GRID_HY1   74
#define GRID_HY2   124

/* Per-cell top-left pixel coords. */
static const unsigned int CELL_X[3] = { GRID_X0,  GRID_VX1 + 2, GRID_VX2 + 2 };
static const unsigned int CELL_Y[3] = { GRID_Y0,  GRID_HY1 + 2, GRID_HY2 + 2 };
/* Per-cell width / height (last column/row absorb the remainder). */
static const unsigned char CELL_W[3] = { GRID_VX1 - GRID_X0,
                                          GRID_VX2 - GRID_VX1 - 2,
                                          GRID_X1 - GRID_VX2 - 1 };
static const unsigned char CELL_H[3] = { GRID_HY1 - GRID_Y0,
                                          GRID_HY2 - GRID_HY1 - 2,
                                          GRID_Y1 - GRID_HY2 - 1 };

/* ---------- Forward decls ---------- */
static void plot(unsigned int x, unsigned int y, unsigned char on);
static void fill_rect(unsigned int x0, unsigned int y0,
                      unsigned int x1, unsigned int y1,
                      unsigned char on);
static void init_video(void);

/* Mark glyph occupies the inner 40x40 of each cell, centered. */
#define MARK_SIZE  40

static void cell_mark_origin(unsigned char cell,
                              unsigned int *x, unsigned int *y) {
    unsigned char col = cell % 3;
    unsigned char row = cell / 3;
    *x = CELL_X[col] + (CELL_W[col] - MARK_SIZE) / 2;
    *y = CELL_Y[row] + (CELL_H[row] - MARK_SIZE) / 2;
}

/* Clears the full cell content (mark area + digit hint + any stray pixels).
 * Used both when placing a mark and when starting a new game. */
static void clear_cell_mark(unsigned char cell) {
    unsigned char col = cell % 3;
    unsigned char row = cell / 3;
    fill_rect(CELL_X[col], CELL_Y[row],
              CELL_X[col] + CELL_W[col] - 1,
              CELL_Y[row] + CELL_H[row] - 1, 0);
}

/* Recolor every 8x8 char cell that the mark area overlaps.
 * We over-paint a generous bounding box so the entire cell takes the new fg. */
static void set_cell_color(unsigned char cell, unsigned char fg) {
    unsigned char col = cell % 3;
    unsigned char row = cell / 3;
    unsigned int cx = CELL_X[col];
    unsigned int cy = CELL_Y[row];
    unsigned char char_col0 = (unsigned char)(cx >> 3);
    unsigned char char_col1 = (unsigned char)((cx + CELL_W[col] - 1) >> 3);
    unsigned char char_row0 = (unsigned char)(cy >> 3);
    unsigned char char_row1 = (unsigned char)((cy + CELL_H[row] - 1) >> 3);
    unsigned char r, c;
    unsigned char attr = (fg << 4) | COL_BLUE;
    for (r = char_row0; r <= char_row1; r++) {
        for (c = char_col0; c <= char_col1; c++) {
            SCREEN_RAM[(unsigned int)r * 40 + c] = attr;
        }
    }
}

/* Thick-stroke X: two diagonals, each 4 px wide.
 * A 40x40 area: main diagonal from (0,0) to (39,39); anti-diagonal (39,0) to (0,39).
 * We thicken by drawing 4 parallel diagonals.
 */
static void draw_x(unsigned char cell) {
    unsigned int ox, oy;
    unsigned char i, k;
    cell_mark_origin(cell, &ox, &oy);
    clear_cell_mark(cell);
    set_cell_color(cell, COL_LIGHTBLUE);
    for (k = 0; k < 4; k++) {
        for (i = 0; i < MARK_SIZE - k; i++) {
            plot(ox + i + k, oy + i,     1);   /* main diag, shifted right */
            plot(ox + i,     oy + i + k, 1);   /* main diag, shifted down */
            plot(ox + (MARK_SIZE - 1 - i - k), oy + i,         1); /* anti, shift left */
            plot(ox + (MARK_SIZE - 1 - i),     oy + i + k,     1); /* anti, shift down */
        }
    }
}

/* Thick-stroke O: midpoint circle. Inscribed circle of MARK_SIZE area,
 * radius ~ 19, drawn at 4 different radii (r-1..r-4) to make a ring 4 px thick.
 */
static void plot_circle_points(int cx, int cy, int x, int y) {
    plot((unsigned int)(cx + x), (unsigned int)(cy + y), 1);
    plot((unsigned int)(cx - x), (unsigned int)(cy + y), 1);
    plot((unsigned int)(cx + x), (unsigned int)(cy - y), 1);
    plot((unsigned int)(cx - x), (unsigned int)(cy - y), 1);
    plot((unsigned int)(cx + y), (unsigned int)(cy + x), 1);
    plot((unsigned int)(cx - y), (unsigned int)(cy + x), 1);
    plot((unsigned int)(cx + y), (unsigned int)(cy - x), 1);
    plot((unsigned int)(cx - y), (unsigned int)(cy - x), 1);
}

static void draw_circle(int cx, int cy, int r) {
    int x = 0;
    int y = r;
    int d = 1 - r;
    while (x <= y) {
        plot_circle_points(cx, cy, x, y);
        x++;
        if (d < 0) {
            d += 2 * x + 1;
        } else {
            y--;
            d += 2 * (x - y) + 1;
        }
    }
}

static void draw_o(unsigned char cell) {
    unsigned int ox, oy;
    int cx, cy, r;
    cell_mark_origin(cell, &ox, &oy);
    clear_cell_mark(cell);
    set_cell_color(cell, COL_YELLOW);
    cx = (int)ox + MARK_SIZE / 2;
    cy = (int)oy + MARK_SIZE / 2;
    for (r = (MARK_SIZE / 2) - 1; r >= (MARK_SIZE / 2) - 4; r--) {
        draw_circle(cx, cy, r);
    }
}

/* ---------- Bitmap addressing ----------
 * Hi-res layout: 25 char rows x 40 char cols x 8 bytes.
 * Pixel (x,y) is in cell (x>>3, y>>3) at sub-row (y&7).
 * Byte addr = BITMAP + (y>>3)*320 + (x & ~7) + (y & 7)
 * Bit       = 0x80 >> (x & 7)
 */
static void plot(unsigned int x, unsigned int y, unsigned char on) {
    unsigned char *p;
    unsigned char mask;
    if (x >= 320U || y >= 200U) return;
    p = BITMAP + ((y >> 3) * 320U) + (x & 0xFFF8U) + (y & 7U);
    mask = 0x80 >> (x & 7);
    if (on) *p |= mask; else *p &= (unsigned char)~mask;
}

static void hline(unsigned int x0, unsigned int x1, unsigned int y) {
    unsigned int x;
    for (x = x0; x <= x1; x++) plot(x, y, 1);
}

static void vline(unsigned int x, unsigned int y0, unsigned int y1) {
    unsigned int y;
    for (y = y0; y <= y1; y++) plot(x, y, 1);
}

/* Fast rectangle fill/clear.
 * Walks the bitmap one byte-column at a time: the left and right ends use
 * a partial-byte mask; the middle columns get full-byte writes (0xFF or 0x00).
 * This skips the per-pixel address math that plain plot() does. */
static void fill_rect(unsigned int x0, unsigned int y0,
                      unsigned int x1, unsigned int y1,
                      unsigned char on) {
    unsigned int xc0 = x0 >> 3;
    unsigned int xc1 = x1 >> 3;
    unsigned char x_lo = (unsigned char)(x0 & 7);
    unsigned char x_hi = (unsigned char)(x1 & 7);
    unsigned char fill = on ? 0xFF : 0x00;
    unsigned char left_mask, right_mask, mid_mask;
    unsigned char *row_base;
    unsigned char *p;
    unsigned int y, xc;

    if (xc0 == xc1) {
        /* Whole rect fits in one byte-column. */
        mid_mask = (unsigned char)((0xFF >> x_lo) & (unsigned char)(0xFF << (7 - x_hi)));
        for (y = y0; y <= y1; y++) {
            row_base = BITMAP + ((y >> 3) * 320U) + (y & 7U);
            p = row_base + (xc0 << 3);
            if (on) *p |= mid_mask; else *p &= (unsigned char)~mid_mask;
        }
        return;
    }

    /* Spans multiple byte-columns. */
    left_mask  = (unsigned char)(0xFF >> x_lo);          /* bits x_lo..7 */
    right_mask = (unsigned char)(0xFF << (7 - x_hi));    /* bits 0..x_hi */

    for (y = y0; y <= y1; y++) {
        row_base = BITMAP + ((y >> 3) * 320U) + (y & 7U);

        /* Left partial byte. */
        p = row_base + (xc0 << 3);
        if (on) *p |= left_mask; else *p &= (unsigned char)~left_mask;

        /* Middle full bytes. */
        for (xc = xc0 + 1; xc < xc1; xc++) {
            *(row_base + (xc << 3)) = fill;
        }

        /* Right partial byte. */
        p = row_base + (xc1 << 3);
        if (on) *p |= right_mask; else *p &= (unsigned char)~right_mask;
    }
}

static void draw_grid(void) {
    /* Two vertical dividers, 2 px thick. */
    vline(GRID_VX1,     GRID_Y0, GRID_Y1);
    vline(GRID_VX1 + 1, GRID_Y0, GRID_Y1);
    vline(GRID_VX2,     GRID_Y0, GRID_Y1);
    vline(GRID_VX2 + 1, GRID_Y0, GRID_Y1);
    /* Two horizontal dividers, 2 px thick. */
    hline(GRID_X0, GRID_X1, GRID_HY1);
    hline(GRID_X0, GRID_X1, GRID_HY1 + 1);
    hline(GRID_X0, GRID_X1, GRID_HY2);
    hline(GRID_X0, GRID_X1, GRID_HY2 + 1);
}

/* ---------- 5x7 mini-font ----------
 * Each glyph: 7 bytes (rows top->bottom). Pixels are top 5 bits of each byte.
 * We only define the chars we need: 0-9, dash, space, and uppercase letters
 * used in status messages: A, C, D, E, I, K, L, N, O, P, R, S, T, W, X, Y.
 *
 * Glyphs encoded as binary literals via hex. Bit 7 = leftmost pixel.
 * Example: 0x70 = 01110000 = "_XXX____" -> 3px horizontal segment.
 */
typedef struct { char ch; unsigned char rows[7]; } Glyph;

static const Glyph FONT[] = {
    {' ', {0,0,0,0,0,0,0}},
    {'-', {0,0,0,0x70,0,0,0}},
    {'0', {0x70,0x88,0x98,0xA8,0xC8,0x88,0x70}},
    {'1', {0x20,0x60,0x20,0x20,0x20,0x20,0x70}},
    {'2', {0x70,0x88,0x08,0x10,0x20,0x40,0xF8}},
    {'3', {0xF8,0x10,0x20,0x10,0x08,0x88,0x70}},
    {'4', {0x10,0x30,0x50,0x90,0xF8,0x10,0x10}},
    {'5', {0xF8,0x80,0xF0,0x08,0x08,0x88,0x70}},
    {'6', {0x30,0x40,0x80,0xF0,0x88,0x88,0x70}},
    {'7', {0xF8,0x08,0x10,0x20,0x40,0x40,0x40}},
    {'8', {0x70,0x88,0x88,0x70,0x88,0x88,0x70}},
    {'9', {0x70,0x88,0x88,0x78,0x08,0x10,0x60}},
    {'A', {0x70,0x88,0x88,0xF8,0x88,0x88,0x88}},
    {'C', {0x70,0x88,0x80,0x80,0x80,0x88,0x70}},
    {'D', {0xF0,0x88,0x88,0x88,0x88,0x88,0xF0}},
    {'E', {0xF8,0x80,0x80,0xF0,0x80,0x80,0xF8}},
    {'I', {0x70,0x20,0x20,0x20,0x20,0x20,0x70}},
    {'K', {0x88,0x90,0xA0,0xC0,0xA0,0x90,0x88}},
    {'L', {0x80,0x80,0x80,0x80,0x80,0x80,0xF8}},
    {'N', {0x88,0xC8,0xA8,0xA8,0x98,0x88,0x88}},
    {'O', {0x70,0x88,0x88,0x88,0x88,0x88,0x70}},
    {'P', {0xF0,0x88,0x88,0xF0,0x80,0x80,0x80}},
    {'R', {0xF0,0x88,0x88,0xF0,0xA0,0x90,0x88}},
    {'S', {0x70,0x88,0x80,0x70,0x08,0x88,0x70}},
    {'T', {0xF8,0x20,0x20,0x20,0x20,0x20,0x20}},
    {'W', {0x88,0x88,0x88,0xA8,0xA8,0xD8,0x88}},
    {'X', {0x88,0x88,0x50,0x20,0x50,0x88,0x88}},
    {'Y', {0x88,0x88,0x50,0x20,0x20,0x20,0x20}},
};
#define FONT_COUNT (sizeof(FONT) / sizeof(FONT[0]))

static const Glyph *find_glyph(char ch) {
    unsigned char i;
    for (i = 0; i < FONT_COUNT; i++) {
        if (FONT[i].ch == ch) return &FONT[i];
    }
    return &FONT[0];   /* space fallback */
}

static void draw_glyph(unsigned int x, unsigned int y, char ch) {
    const Glyph *g = find_glyph(ch);
    unsigned char row, col;
    for (row = 0; row < 7; row++) {
        unsigned char bits = g->rows[row];
        for (col = 0; col < 5; col++) {
            if (bits & (0x80 >> col)) plot(x + col, y + row, 1);
        }
    }
}

static void draw_text(unsigned int x, unsigned int y, const char *s) {
    while (*s) {
        draw_glyph(x, y, *s);
        x += 6;        /* 5 px glyph + 1 px space */
        s++;
    }
}

static void draw_title(void) {
    /* "TIC-TAC-TOE" is 11 chars * 6 px = 66 px wide. Center: (320-66)/2 = 127. */
    draw_text(127, 4, "TIC-TAC-TOE");
}

static void draw_hint(unsigned char cell) {
    /* Cell numbering: 0..8 maps to digits '1'..'9'. Hint sits in upper-left
       of the cell with 4 px inset. */
    unsigned char col = cell % 3;
    unsigned char row = cell / 3;
    unsigned int x = CELL_X[col] + 4;
    unsigned int y = CELL_Y[row] + 4;
    char ch = (char)('1' + cell);
    draw_glyph(x, y, ch);
}

static void draw_all_hints(void) {
    unsigned char i;
    for (i = 0; i < 9; i++) draw_hint(i);
}

/* Clear bitmap + cell color attrs for everything below the title row.
 * Title sits at char rows 0-1 (y<=10); we clear from char row 3 (y=24) down.
 * This wipes the previous game's marks, "PRESS ANY KEY" message, and any
 * left-over light-blue/yellow cell colors. */
static void clear_board_area(void) {
    /* Bitmap: char rows 3..24 = 22 rows * 320 bytes = 7040 bytes from offset 960. */
    memset(BITMAP + 960U, 0, BITMAP_LEN - 960U);
    /* Screen RAM: char rows 3..24 = 22 rows * 40 cells = 880 cells from offset 120. */
    memset(SCREEN_RAM + 120U, (COL_WHITE << 4) | COL_BLUE, 880U);
}

/* Status row at the bottom of the screen, char row 24 (y=192..198). */
#define STATUS_Y     193

static void clear_status(void) {
    unsigned char c;
    fill_rect(0, STATUS_Y - 1, 319, STATUS_Y + 7, 0);
    /* Reset that char row's color attr to white-on-blue. */
    for (c = 0; c < 40; c++) SCREEN_RAM[24U * 40 + c] = (COL_WHITE << 4) | COL_BLUE;
}

static void set_status_color(unsigned char fg) {
    unsigned char c;
    unsigned char attr = (fg << 4) | COL_BLUE;
    for (c = 0; c < 40; c++) SCREEN_RAM[24U * 40 + c] = attr;
}

static void draw_status(const char *msg, unsigned char fg) {
    unsigned char len = 0;
    unsigned int width_px;
    unsigned int x;
    while (msg[len]) len++;
    width_px = (unsigned int)len * 6U;
    x = (320U - width_px) / 2U;
    clear_status();
    set_status_color(fg);
    draw_text(x, STATUS_Y, msg);
}

static void place_mark(unsigned char cell, unsigned char player) {
    board[cell] = player;
    if (player == 'X') draw_x(cell); else draw_o(cell);
}

/* Status line shows the result; we also draw "PRESS ANY KEY" one row up
 * (the status row's own line is for the result). */
static void show_endgame(unsigned char winner) {
    const char *msg;
    unsigned char fg;
    if (winner == 'X')      { msg = "X WINS"; fg = COL_LIGHTBLUE; }
    else if (winner == 'O') { msg = "O WINS"; fg = COL_YELLOW; }
    else                    { msg = "DRAW";   fg = COL_WHITE; }
    draw_status(msg, fg);

    /* "PRESS ANY KEY" two char-rows above the status row. */
    {
        const char *hint = "PRESS ANY KEY";
        unsigned char len = 0;
        unsigned int width_px;
        unsigned int x;
        unsigned int y = STATUS_Y - 16;
        while (hint[len]) len++;
        width_px = (unsigned int)len * 6U;
        x = (320U - width_px) / 2U;
        fill_rect(0, y - 1, 319, y + 7, 0);
        draw_text(x, y, hint);
    }
}

#ifndef UNIT_TEST
/* Block until the player presses a valid 1-9 for an empty cell. */
static void play_turn(unsigned char player) {
    for (;;) {
        char k = cgetc();
        if (k >= '1' && k <= '9') {
            unsigned char cell = (unsigned char)(k - '1');
            if (board[cell] == 0) {
                place_mark(cell, player);
                return;
            }
        }
        /* else: ignore, keep waiting */
    }
}

int main(void) {
    init_video();
    draw_title();

    for (;;) {
        unsigned char player = 'X';
        unsigned char winner = 0;

        reset_board();
        clear_board_area();
        draw_grid();
        draw_all_hints();

        while (!winner) {
            draw_status(player == 'X' ? "PLAYER X" : "PLAYER O",
                        player == 'X' ? COL_LIGHTBLUE : COL_YELLOW);
            play_turn(player);
            winner = check_winner();
            if (!winner) player = (player == 'X') ? 'O' : 'X';
        }
        show_endgame(winner);
        cgetc();
    }
    return 0;
}
#endif /* UNIT_TEST */

static void init_video(void) {
    /* Border + background dark blue */
    VIC_BORDER = COL_BLUE;
    VIC_BG     = COL_BLUE;

    /* Clear the bitmap (all pixels off) */
    memset(BITMAP, 0x00, BITMAP_LEN);

    /* Set every screen-RAM cell to (fg=white, bg=blue) so any
       pixel we later plot shows up as white on blue. */
    memset(SCREEN_RAM, (COL_WHITE << 4) | COL_BLUE, 1000);

    /* CR1: DEN (bit 4) + 25 rows (bit 3) + default yscroll 3 = 0x1B;
       OR-in BMM (bit 5) = 0x3B. Final value 0x3B. */
    VIC_CR1 = 0x3B;

    /* CR2: 40 columns, no MCM, default xscroll = 0xC8. */
    VIC_CR2 = 0xC8;

    /* MEMPTR: screen RAM at $0400 (bits 7-4 = $1), bitmap at $2000 (bit 3 = 1).
       Value = 0001 1000 = $18. */
    VIC_MEMPTR = 0x18;
}
