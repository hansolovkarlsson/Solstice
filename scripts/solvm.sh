
if [ "$1" = "-v" ]; then
	VERBOSE=-v
	FILENAME="$2"
else
	FILENAME="$1"
fi
BASEFILE="${FILENAME%.*}"

solvm $VERBOSE "$BASEFILE.bin"
