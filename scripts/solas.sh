
if [ "$1" = "-v" ]; then
	VERBOSE=-v
	FILENAME="$2"
else
	FILENAME="$1"
fi
BASEFILE="${FILENAME%.*}"

solas $VERBOSE "$BASEFILE.sasm" "$BASEFILE.bin"
solvm $VERBOSE "$BASEFILE.bin"
