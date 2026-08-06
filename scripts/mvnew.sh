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
mv -f *.sasm 		"$BASEDIR/examples/asm"
mv -f doc_*.pas		"$BASEDIR/examples/doc"
mv -f test_*.pas	"$BASEDIR/examples/test"
cd "$BASEDIR/new"
ls -rl files
