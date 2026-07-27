filename=$1
basefile="${filename%.*}"
../bin/desole "$basefile.bin" "$basefile.disasm"
