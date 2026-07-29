
if [ "$1" = "-v" ]; then
	VERBOSE=-v
	FILENAME="$2"
else
	FILENAME="$1"
fi
BASEFILE="${FILENAME%.*}"

desole $VERBOSE "$BASEFILE.bin" "$BASEFILE.disasm"
cat "$BASEFILE.disasm"

