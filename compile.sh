#!/bin/sh

gcc -g -std=c99 -Wall `pkg-config --cflags --libs libpng` -o main main.c

