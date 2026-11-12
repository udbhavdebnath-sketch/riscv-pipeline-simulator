CC      := gcc
# -Wno-missing-field-initializers: the base ISA's six-field handler literals
# leave the newer flags/rs2_sel fields zero on purpose.
# -frounding-math: FP results depend on the runtime rounding mode.
CFLAGS  := -std=c11 -Wall -Wextra -Wno-missing-field-initializers -frounding-math -g -O0 \
           -Icore -Iisa -Iassembler -Iui -Ipipeline \
           -MMD -MP   # .d files: rebuild objects when a header they include changes
LDFLAGS := -lm

BIN := rvsim

SRCS := \
	core/cpu_state.c \
	core/memory.c \
	core/isa_registry.c \
	core/decoder.c \
	core/executor.c \
	isa/rv32i_base.c \
	isa/rv32m.c \
	isa/zicsr.c \
	isa/fp_helpers.c \
	isa/rv32f.c \
	assembler/assembler.c \
	ui/cli_display.c \
	pipeline/control.c \
	pipeline/pipeline.c \
	pipeline/hazard.c \
	pipeline/branch_predictor.c \
	pipeline/bp_static.c \
	pipeline/bp_dynamic.c \
	main.c

OBJS := $(SRCS:.c=.o)
-include $(OBJS:.o=.d)

.PHONY: all clean run memcheck test

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Example: make run PROG=tests/sample1.s
run: $(BIN)
	./$(BIN) $(PROG) --batch

# Requires valgrind installed. Example: make memcheck PROG=tests/sample1.s
memcheck: $(BIN)
	valgrind --error-exitcode=1 --leak-check=full ./$(BIN) $(PROG) --batch

# Diffs every program under every hazard unit and predictor against the
# single-cycle oracle and checks the cycle identities.
test: $(BIN)
	./compare.sh

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(BIN)
