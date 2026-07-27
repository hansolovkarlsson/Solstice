filename=$1
basefile="${filename%.*}"
echo "*** RUN $basefile"
../bin/pascalc -r "$basefile.bin"
