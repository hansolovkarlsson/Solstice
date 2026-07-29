cd "$BASEDIR/src"
make clean && make
cp pascalc solas solvm desole test_recovery "$BASEDIR/bin"
