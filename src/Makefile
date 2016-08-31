#
# Matilda build makefile
#


# Uncomment this line to compile in Mac OSX, having previously installed
# clang-omp through brew.
# CC := clang-omp
# CC := clang

CFLAGS := -Iinc/ -std=c99 -O2 -Wall -Wextra -Wformat=2 -pedantic-errors \
	-Wfatal-errors -Wundef -Wno-unused-result -fno-stack-protector \
	-march=native -MMD -MP -fopenmp

LDFLAGS += -lm

# For debugging add -g to CFLAGS
# CFLAGS += -g

# GCC > 4.9 only
# CFLAGS += -fdiagnostics-color=auto

SRCFILES := $(wildcard *.c mcts/*.c)

OBJFILES := $(patsubst %.c,%.o,$(SRCFILES))

DEPFILES := $(patsubst %.o,%.d,$(OBJFILES))

PROGRAMS := matilda test gen_opening_book learn_best_plays learn_pat_weights \
	gen_zobrist_table gen_joseki_book

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

gen_joseki_book: $(OBJFILES) joseki/*.c
	@$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@

%.o: %.c
	@$(CC) -c -o $@ $< $(CFLAGS)

-include $(DEPFILES)

tidy:
	@$(RM) -f callgrind.out.* matilda*.log tuning.log *.o mcts/*.o *.d mcts/*.d

clean: tidy
	@$(RM) -f $(PROGRAMS)
