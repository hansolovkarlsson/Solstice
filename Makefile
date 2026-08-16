CC      := cc
CFLAGS  := -Wall -Wextra -std=c11 -g -fno-common -Isrc/common
LDFLAGS :=

BINDIR := bin

# Shared by every binary: the bytecode file format (code[]/sym_table[]/
# string_pool[] plus save/load) and the recoverable-error facility.
COMMON_SRCS := src/common/bytecode.c src/common/error.c
COMMON_OBJS := $(COMMON_SRCS:.c=.o)

# Compiler front end only: lexer -> parser -> type checker -> optimizer ->
# codegen -> AST printer. The VM binary never links any of this.
FRONTEND_SRCS := src/pascalc/lexer.c src/pascalc/parser.c src/pascalc/type_checker.c \
                  src/pascalc/optimizer.c src/pascalc/codegen.c src/pascalc/ast_printer.c
FRONTEND_OBJS := $(FRONTEND_SRCS:.c=.o)

# VM only: the bytecode interpreter. The compiler binary never links this.
VM_SRCS := src/solvm/vm.c
VM_OBJS := $(VM_SRCS:.c=.o)

# BASIC front end only, mirroring FRONTEND_SRCS's own split (see
# src/basicc/basic.h for why this is its own small parallel token/AST
# vocabulary rather than sharing pascalc's TokenType/NodeType/ASTNode) -
# no optimizer in milestone 1.
BASICC_FRONTEND_SRCS := src/basicc/lexer.c src/basicc/parser.c src/basicc/type_checker.c \
                         src/basicc/codegen.c src/basicc/ast_printer.c
BASICC_FRONTEND_OBJS := $(BASICC_FRONTEND_SRCS:.c=.o)

PASCALC_OBJS       := src/pascalc/pascalc.o $(FRONTEND_OBJS) $(COMMON_OBJS)
SOLVM_OBJS         := src/solvm/solvm.o $(VM_OBJS) $(COMMON_OBJS)
SOLAS_OBJS         := src/solas/solas.o $(COMMON_OBJS)
DESOLE_OBJS        := src/desole/desole.o $(COMMON_OBJS)
TEST_RECOVERY_OBJS := src/pascalc/test_recovery.o $(FRONTEND_OBJS) $(COMMON_OBJS)
BASICC_OBJS        := src/basicc/basicc.o $(BASICC_FRONTEND_OBJS) $(COMMON_OBJS)

.PHONY: all clean pascalc solvm solas desole test_recovery basicc

all: $(BINDIR)/pascalc $(BINDIR)/solvm $(BINDIR)/solas $(BINDIR)/desole $(BINDIR)/test_recovery $(BINDIR)/basicc

# Convenience phony targets so `make pascalc` etc. still builds just one
# binary, matching every other tool's own build instructions.
pascalc: $(BINDIR)/pascalc
solvm: $(BINDIR)/solvm
solas: $(BINDIR)/solas
desole: $(BINDIR)/desole
test_recovery: $(BINDIR)/test_recovery
basicc: $(BINDIR)/basicc

$(BINDIR)/pascalc: $(PASCALC_OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

$(BINDIR)/solvm: $(SOLVM_OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

$(BINDIR)/solas: $(SOLAS_OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $^

$(BINDIR)/desole: $(DESOLE_OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $^

$(BINDIR)/test_recovery: $(TEST_RECOVERY_OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

$(BINDIR)/basicc: $(BASICC_OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

$(BINDIR):
	mkdir -p $(BINDIR)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Header dependencies: get everything to rebuild if a shared header
# changes, without needing a separate dependency-generation step.
HEADERS := $(wildcard src/common/*.h src/pascalc/*.h src/solvm/*.h src/solas/*.h src/desole/*.h src/basicc/*.h)
$(PASCALC_OBJS) $(SOLVM_OBJS) $(SOLAS_OBJS) $(DESOLE_OBJS) $(TEST_RECOVERY_OBJS) $(BASICC_OBJS): $(HEADERS)

clean:
	rm -f $(BINDIR)/pascalc $(BINDIR)/solvm $(BINDIR)/solas $(BINDIR)/desole $(BINDIR)/test_recovery $(BINDIR)/basicc
	rm -f $(COMMON_OBJS) $(FRONTEND_OBJS) $(VM_OBJS) $(BASICC_FRONTEND_OBJS) \
	      src/pascalc/pascalc.o src/solvm/solvm.o src/solas/solas.o src/desole/desole.o \
	      src/pascalc/test_recovery.o src/basicc/basicc.o
