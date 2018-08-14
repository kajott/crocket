#!/bin/sh
set -x
exec gcc -std=c99 -Wall -Wextra -pedantic -Werror -g -O3 -march=native -Isrc -Iexample src/crocket.c example/crocket_test.c -o crocket_test
