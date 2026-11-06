#!/bin/sh
# Build hs_sieve (MIT). Needs g++ (C++17). Sieve itself is built separately from github.com/Thor110/Potentia (AGPL-3.0).
g++ -O2 -std=c++17 -pthread -o hs_sieve hs_sieve.cpp
