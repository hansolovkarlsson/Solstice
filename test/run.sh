filename=$1
basefile="${filename%.*}"
../bin/solvm "$basefile.bin"
