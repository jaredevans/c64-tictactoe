CC65   = cl65
TARGET = c64
EMU    = /Applications/vice-arm64-gtk3-3.10/bin/x64sc

ttt.prg: ttt.c
	$(CC65) -t $(TARGET) -O ttt.c -o ttt.prg

run: ttt.prg
	$(EMU) ttt.prg

test_winner: test_winner.c ttt.c
	cc -DUNIT_TEST -o test_winner test_winner.c

test: test_winner
	./test_winner

clean:
	rm -f ttt.prg ttt.o ttt.s test_winner

.PHONY: run test clean
