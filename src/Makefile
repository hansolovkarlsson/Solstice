CC      := cc
CFLAGS  := -Wall -Wextra -std=c11 -g
LDFLAGS :=

# Shared by both the compiler and the VM: the bytecode file format
# (code[]/sym_table[] plus save/load) and the recoverable-error facility.
SHARED_SRCS := bytecode.c error.c
SHARED_OBJS := $(SHARED_SRCS:.c=.o)

# Compiler front end only: lexer -> parser -> type checker -> optimizer ->
# codegen -> AST printer. The VM binary never links any of this.
FRONTEND_SRCS := lexer.c parser.c type_checker.c optimizer.c codegen.c ast_printer.c
FRONTEND_OBJS := $(FRONTEND_SRCS:.c=.o)

# VM only: the bytecode interpreter. The compiler binary never links this.
VM_SRCS := vm.c
VM_OBJS := $(VM_SRCS:.c=.o)

PASCALC        := pascalc
PASCALC_OBJS   := pascalc.o $(FRONTEND_OBJS) $(SHARED_OBJS)

PASCALVM       := pascalvm
PASCALVM_OBJS  := pascalvm.o $(VM_OBJS) $(SHARED_OBJS)

TEST_RECOVERY      := test_recovery
TEST_RECOVERY_OBJS := test_recovery.o $(FRONTEND_OBJS) $(SHARED_OBJS)

.PHONY: all clean

all: $(PASCALC) $(PASCALVM) $(TEST_RECOVERY)

$(PASCALC): $(PASCALC_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(PASCALVM): $(PASCALVM_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TEST_RECOVERY): $(TEST_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Header dependencies: get everything to rebuild if a shared header
# changes, without needing a separate dependency-generation step.
HEADERS := $(wildcard *.h)
$(PASCALC_OBJS) $(PASCALVM_OBJS) $(TEST_RECOVERY_OBJS): $(HEADERS)

clean:
	rm -f $(PASCALC) $(PASCALVM) $(TEST_RECOVERY) *.o
