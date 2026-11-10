#!/bin/bash
# Builds the test programs into ./bin (Linux, g++).
set -e; mkdir -p bin
g++ -O2 -march=native -o bin/ctxcm src/ctxcm.cpp
g++ -O2 -march=native -o bin/para src/para.cpp
g++ -O2 -w -o bin/lpaq1-para src/lpaq1-para.cpp
g++ -O2 -w -DCTRL -o bin/lpaq1-wordpair src/lpaq1-para.cpp
g++ -O2 -w -o bin/lpaq1-wp-para src/lpaq1-para-wordpair.cpp
g++ -O2 -w -DCTRL -o bin/lpaq1-wp-sparse src/lpaq1-para-wordpair.cpp
g++ -O2 -w -o bin/lpaq1-wiki src/lpaq1-wiki.cpp
g++ -O2 -w -DCTRL -o bin/lpaq1-wiki-control src/lpaq1-wiki.cpp
ls -l bin
