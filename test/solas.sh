filename=$1
basefile="${filename%.*}"
../bin/solas "$basefile.sasm" "$basefile.bin"
../bin/solvm "$basefile.bin"
