filename=$1
basefile="${filename%.*}"
echo "*** BUILD: $basefile"
../bin/pascalc "$basefile.pas" "$basefile.bin"

