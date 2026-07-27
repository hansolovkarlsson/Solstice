
if [ "$1" = "-v" ]; then
	VERBOSE=-v
	FILENAME="$2"
else
	FILENAME="$1"
fi
BASEFILE="${FILENAME%.*}"

../bin/solas $VERBOSE "$BASEFILE.sasm" "$BASEFILE.bin"
../bin/solvm $VERBOSE "$BASEFILE.bin"
