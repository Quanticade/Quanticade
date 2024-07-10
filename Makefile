SRCS := Source/attacks.c Source/evaluate.c Source/movegen.c Source/perft.c Source/pvtable.c Source/quanticade.c Source/search.c Source/threads.c Source/uci.c Source/utils.c Source/nnue/misc.cpp Source/nnue/nnue.cpp
HDRS := Source/attacks.h Source/bitboards.h Source/enums.h Source/evaluate.h Source/movegen.h Source/nnue_consts.h Source/nnue.h Source/perft.h Source/pvtable.h Source/search.h Source/syzygy.h Source/threads.h Source/uci.h Source/utils.h Source/nnue/misc.h Source/nnue/nnue.h

all:
	clang -march=native -flto -Ofast -funroll-loops -fomit-frame-pointer $(SRCS) -o Quanticade -lm -DUSE_SSE41 -msse4.1 -msse4.2 -DUSE_SSSE3 -mssse3 -DUSE_SSE2 -msse2 -DUSE_SSE -msse -DUSE_AVX2 -mavx2