cd "$BASEDIR/new/files"
mv -f README.md		"$BASEDIR"
mv -f *.md		"$BASEDIR/docs"
mv -f *.c *.h Makefile	"$BASEDIR/src"
mv -f *.sasm 		"$BASEDIR/examples/asm"
mv -f doc_*.pas		"$BASEDIR/examples/doc"
mv -f test_*.pas	"$BASEDIR/examples/test"
cd "$BASEDIR/new"
ls -rl files
