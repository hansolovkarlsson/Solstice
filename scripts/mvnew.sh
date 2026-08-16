cd "$BASEDIR/new/files"
mv -f README.md		"$BASEDIR"
mv -f *.md		"$BASEDIR/docs"
mv -f Makefile		"$BASEDIR" 2>/dev/null
mv -f bytecode.c bytecode.h error.c error.h common.h \
			"$BASEDIR/src/common" 2>/dev/null
mv -f lexer.c lexer.h parser.c parser.h type_checker.c type_checker.h \
      optimizer.c optimizer.h codegen.c codegen.h ast_printer.c ast_printer.h \
      pascalc.c compiler.h test_recovery.c \
			"$BASEDIR/src/pascalc" 2>/dev/null
mv -f vm.c vm.h solvm.c	"$BASEDIR/src/solvm" 2>/dev/null
mv -f solas.c		"$BASEDIR/src/solas" 2>/dev/null
mv -f desole.c		"$BASEDIR/src/desole" 2>/dev/null
mv -f doc_*.pas		"$BASEDIR/examples/doc"
# test_<feature>_<variant>.pas  -> examples/Pascal/<feature>/ (mirrors the
#                                   grouping already on disk)
# test_<feature>_<variant>.sasm -> examples/asm/<feature>/    (assembler/VM
#                                   regression tests, grouped the same way)
# test_<variant>.bas            -> examples/BASIC/ (flat - no per-feature
#                                   subdivision yet, see examples/BASIC/)
for f in test_*.pas test_*.sasm; do
	[ -f "$f" ] || continue
	rest="${f#test_}"
	base="${rest%.*}"
	prefix="${base%%_*}"
	case "$f" in
		*.pas)  dest="$BASEDIR/examples/Pascal/$prefix" ;;
		*.sasm) dest="$BASEDIR/examples/asm/$prefix" ;;
	esac
	mkdir -p "$dest"
	mv -f "$f" "$dest/"
done
mv -f test_*.bas	"$BASEDIR/examples/BASIC" 2>/dev/null
mv -f *.sasm 		"$BASEDIR/examples/asm" 2>/dev/null
cd "$BASEDIR/new"
ls -rl files
