/*
 * Host-side unit tests for check_winner / WINNING_LINES.
 *
 * Build/run via: make test
 *
 * We #include ttt.c with -DUNIT_TEST defined so the C64-specific
 * memory pokes are skipped. ttt.c guards those with #ifndef UNIT_TEST.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define UNIT_TEST 1
#include "ttt.c"

static void set_board(const char *s) {
    /* 9-char string: '.' = empty, 'X'/'O' = mark. */
    int i;
    for (i = 0; i < 9; i++) {
        board[i] = (s[i] == '.') ? 0 : (unsigned char)s[i];
    }
}

static void expect(const char *name, const char *layout, unsigned char want) {
    unsigned char got;
    set_board(layout);
    got = check_winner();
    if (got != want) {
        printf("FAIL %s: board=%s want=%c got=%c\n",
               name, layout,
               want ? want : '0',
               got  ? got  : '0');
        exit(1);
    } else {
        printf("ok   %s\n", name);
    }
}

int main(void) {
    expect("empty board", ".........", 0);
    expect("one X",       "X........", 0);
    expect("X row top",   "XXX......", 'X');
    expect("X row mid",   "...XXX...", 'X');
    expect("X row bot",   "......XXX", 'X');
    expect("O col 0",     "O..O..O..", 'O');
    expect("O col 1",     ".O..O..O.", 'O');
    expect("O col 2",     "..O..O..O", 'O');
    expect("X diag \\",   "X...X...X", 'X');
    expect("O diag /",    "..O.O.O..", 'O');
    expect("draw",        "XOXXOXOXO", 'D');
    expect("in progress", "XOXOXO...", 0);
    expect("X wins over full-but-not-yet-full",
                          "XXX.OO...", 'X');
    puts("all check_winner tests passed");
    return 0;
}
