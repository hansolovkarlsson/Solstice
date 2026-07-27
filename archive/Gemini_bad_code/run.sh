filename=$1
basefile="${filename%.*}"
echo "*** RUN $basefile"
bin/pascal -r "$basefile.bin"

