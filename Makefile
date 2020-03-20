all:
	clang++ test.cc -O3 -o test -std=c++17 -march=haswell -g
	clang++ bench.cc -O3 -o bench -std=c++17 -march=haswell -g
