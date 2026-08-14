program TestUntypedfileBadcount;

{ count larger than the target array's own declared size - confirms
  this VM's existing array-bounds-check runtime error fires (not a
  crash, not silently reading/writing out of bounds), since
  BlockRead/BlockWrite desugars into an ordinary indexed array
  assignment that already runs through that check. }

var
    f: file;
    buf: array[0..2] of integer;

begin
    buf[0] := 1;
    buf[1] := 2;
    buf[2] := 3;
    assign(f, '/tmp/untypedfile_badcount.bin');
    rewrite(f);
    BlockWrite(f, buf, 5);
    close(f);
end.
