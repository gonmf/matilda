#
# Matilda build makefile
#


# Override this if your default compiler does not support OpenMP 3.0. On macOS
# a suitable compiler is looked for automatically, see below.
# CC := gcc

UNAME_S := $(shell uname -s)

# Apple ships clang without any OpenMP support, and Matilda needs more than the
# pragmas -- omp locks and omp_get_thread_num are used throughout. Homebrew LLVM
# is used instead when present, in either the Intel or Apple Silicon prefix.
ifeq ($(UNAME_S),Darwin)

BREW_CLANG := $(firstword $(wildcard /usr/local/opt/llvm/bin/clang \
	/opt/homebrew/opt/llvm/bin/clang))

ifeq ($(BREW_CLANG),)
$(error No OpenMP capable compiler found; install one with: brew install llvm)
endif

BREW_PREFIX := $(patsubst %/bin/clang,%,$(BREW_CLANG))
MACOS_SDK := $(shell xcrun --show-sdk-path 2> /dev/null)

ifeq ($(origin CC),default)
CC := $(BREW_CLANG)
endif

# Homebrew clang finds neither the system headers nor, because it omits the
# sysroot when calling the linker, the system libraries. Recent clang versions
# also reject as pedantic errors the declarations without prototypes that are
# used all over Matilda, so that one diagnostic is turned off here.
MACOS_CFLAGS := -isysroot $(MACOS_SDK) -Wno-strict-prototypes
MACOS_LDFLAGS := -L$(MACOS_SDK)/usr/lib -L$(BREW_PREFIX)/lib \
	-Wl,-rpath,$(BREW_PREFIX)/lib

endif

CFLAGS := -Iinc/ -std=c99 -O2 -Wall -Wextra -Wformat=2 -pedantic-errors \
	-Wfatal-errors -Wundef -Wno-unused-result -fno-stack-protector \
	-march=native -MMD -MP -fopenmp $(MACOS_CFLAGS)

LDFLAGS += -lm $(MACOS_LDFLAGS)

# For debugging add -g to CFLAGS
# CFLAGS += -g

# GCC > 4.9 only
# CFLAGS += -fdiagnostics-color=auto


COMMIT := $(shell git rev-parse --short HEAD 2> /dev/null)

CFLAGS += -DCOMMITN='"$(COMMIT)"'

SRCFILES := $(wildcard *.c mcts/*.c)

OBJFILES := $(patsubst %.c,%.o,$(SRCFILES))

DEPFILES := $(patsubst %.o,%.d,$(OBJFILES))

PROGRAMS := matilda test gen_opening_book learn_best_plays learn_pat_weights \
	gen_zobrist_table matilda-twogtp

.PHONY: $(PROGRAMS) clean

all: $(PROGRAMS)

matilda: $(OBJFILES) main/*.c
	@$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@

test: $(OBJFILES) utest/*.c
	@$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@

gen_opening_book: $(OBJFILES) ob_gen/*.c
	@$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@

learn_best_plays: $(OBJFILES) ob_learn/*.c
	@$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@

learn_pat_weights: $(OBJFILES) pat_weights/*.c
	@$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@

gen_zobrist_table: $(OBJFILES) zobrist/*.c
	@$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@

matilda-twogtp: $(OBJFILES) twogtp/*.c
	@$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@

%.o: %.c
	@$(CC) -c -o $@ $< $(CFLAGS)

-include $(DEPFILES)

tidy:
	@$(RM) -f callgrind.out.* matilda*.log tuning.log *.o mcts/*.o *.d \
		mcts/*.d data/matilda*.log

clean: tidy
	@$(RM) -f $(PROGRAMS)
