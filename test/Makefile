

PASCALC  := ../bin/pascalc

PAS_FILES  := $(wildcard *.pas)
BIN_FILES  := $(PAS_FILES:.pas=.bin)


# Make
all: $(BIN_FILES)


# Cleanup
clean:
	-rm -f $(BIN_FILES) *.log

# Tests
tests: $(BIN_FILES)

$(BIN_FILES): $(PAS_FILES)

%.bin: %.pas
	@-rm out.log err.log
	@echo "Compiling $< into $@"
	@-$(PASCALC) $< $@ >> out.log 2>> err.log
	@-cat err.log


