
fluke2png: fluke2png.c
	gcc -g -std=c99 -Wall `pkg-config --cflags --libs libpng` -o fluke2png fluke2png.c

clean:
	@if [ -e "fluke2png" ]; then rm fluke2png; fi

run: fluke2png
	./fluke2png

