filename=$1
basefile="${filename%.*}"
echo "*** RUN $basefile"
../bin/pascalvm "$basefile.bin"
