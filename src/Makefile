# For debugging add -g to CFLAGS

# TODO: unfortunately if you edit header files everything has to be recompiled

#CC = clang

CFLAGS = -Iinc/ -O2 -Wall -Wextra -std=c99 -Wformat=2 -pedantic-errors \
		 -fno-stack-protector -lm -fopenmp -Wfatal-errors -Wundef \
		 -Wno-unused-result -march=native
#-g

SRCFILES = $(wildcard *.c mcts/*.c)

OBJFILES = $(patsubst %.c,%.o,$(SRCFILES))

PROGRAMS = \
	matilda \
	test \
	gen_opening_book \
	learn_best_plays \
	learn_pat_weights \
	gen_zobrist_table \
	gen_joseki_book

.PHONY: $(PROGRAMS) clean

all: $(PROGRAMS)

matilda: $(OBJFILES) entry/*.c
	@$(CC) $^ $(CFLAGS) -o $@

test: $(OBJFILES) utest/*.c
	@$(CC) $^ $(CFLAGS) -o $@

gen_opening_book: $(OBJFILES) ob_gen/*.c
	@$(CC) $^ $(CFLAGS) -o $@

learn_best_plays: $(OBJFILES) ob_learn/*.c
	@$(CC) $^ $(CFLAGS) -o $@

learn_pat_weights: $(OBJFILES) pat_weights/*.c
	@$(CC) $^ $(CFLAGS) -o $@

gen_zobrist_table: $(OBJFILES) zobrist/*.c
	@$(CC) $^ $(CFLAGS) -o $@

gen_joseki_book: $(OBJFILES) joseki/*.c
	@$(CC) $^ $(CFLAGS) -o $@

%.o: %.c
	@$(CC) -c -o $@ $< $(CFLAGS)

clean:
	@$(RM) -f callgrind.out.* matilda-*.log tuning.log $(PROGRAMS) \
	*.o mcts/*.o
