filename=$1
basefile="${filename%.*}"
echo "*** BUILD: $basefile"
bin/pascal -c "$basefile.pas" "$basefile.bin"

