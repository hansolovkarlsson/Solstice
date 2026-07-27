filename=$1
basefile="${filename%.*}"
../bin/pascalc "$basefile.pas" "$basefile.bin"
../bin/solvm "$basefile.bin"
