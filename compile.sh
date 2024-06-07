#!/usr/bin/sh
clang konoran_c.c -Wall -Wextra -pedantic -Wno-unused-value -Wno-unused -Wno-unused-parameter --std=c99 -O2 -g -ggdb -lm

