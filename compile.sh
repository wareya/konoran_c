#!/usr/bin/sh

clang konoran_c.c "$@" -Wall -Wextra -pedantic -Wno-unused-value -Wno-unused -Wno-unused-parameter --std=c99 -O1 -g -ggdb -lm
#gcc konoran_c.c -Wall -Wextra -pedantic -Wno-unused-value -Wno-unused -Wno-unused-parameter --std=c99 -O1 -g -ggdb -lm

