

PASCALC  := ../bin/pascalc
SOLAS := ../bin/solas    

PAS_FILES  := $(wildcard *.pas)
BIN_FILES := $(PAS_FILES:.pas=.bin)
ASM_FILES  := $(wildcard *.sasm)



# Make
all: $(BIN_FILES)


# Cleanup
clean:
	-rm -f $(BIN_FILES) *.disasm


$(BIN_FILES): $(PAS_FILES)


%.bin: %.pas
	@echo "Compiling $< into $@"
	@-$(PASCALC) $< $@ 


