CC      := cc
CFLAGS  := -Wall -Wextra -std=c11 -g
LDFLAGS :=

# Sources shared by every binary (the compiler front end, optimizer,
# codegen, bytecode I/O, VM, and the recoverable-error facility).
COMMON_SRCS := lexer.c parser.c type_checker.c optimizer.c codegen.c \
               bytecode.c ast_printer.c vm.c error.c
COMMON_OBJS := $(COMMON_SRCS:.c=.o)

PASCALC       := pascalc
PASCALC_OBJS  := main.o $(COMMON_OBJS)

TEST_RECOVERY      := test_recovery
TEST_RECOVERY_OBJS := test_recovery.o $(COMMON_OBJS)

.PHONY: all clean

all: $(PASCALC) $(TEST_RECOVERY)

$(PASCALC): $(PASCALC_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TEST_RECOVERY): $(TEST_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Header dependencies: get everything to rebuild if a shared header
# changes, without needing a separate dependency-generation step.
HEADERS := $(wildcard *.h)
$(PASCALC_OBJS) $(TEST_RECOVERY_OBJS): $(HEADERS)

clean:
	rm -f $(PASCALC) $(TEST_RECOVERY) *.o
