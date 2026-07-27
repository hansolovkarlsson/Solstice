filename=$1
basefile="${filename%.*}"
echo "*** BUILD: $basefile"
../bin/pascalc -c "$basefile.pas" "$basefile.bin"

