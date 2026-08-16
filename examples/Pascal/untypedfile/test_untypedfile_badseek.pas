program TestUntypedfileBadseek;

{ seek(f, n) on an untyped file - confirms a clear compile-time
  rejection (v1 scope cut - untyped files don't support seek/filesize
  yet), not a confusing fallback or crash. }

var
    f: file;

begin
    assign(f, '/tmp/untypedfile_badseek.bin');
    rewrite(f);
    seek(f, 0);
    close(f);
end.
