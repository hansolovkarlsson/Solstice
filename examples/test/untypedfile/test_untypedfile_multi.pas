program TestUntypedfileMulti;

{ Multiple BlockRead/BlockWrite calls against the same open file -
  confirms the file position advances correctly across calls, no
  implicit seek/rewind between them. 1 2 3 4 }

var
    f: file;
    a: array[0..1] of integer;
    b: array[0..1] of integer;

begin
    a[0] := 1;
    a[1] := 2;
    b[0] := 3;
    b[1] := 4;

    assign(f, '/tmp/untypedfile_multi.bin');
    rewrite(f);
    BlockWrite(f, a, 2);
    BlockWrite(f, b, 2);
    close(f);

    a[0] := 0; a[1] := 0;
    b[0] := 0; b[1] := 0;

    reset(f);
    BlockRead(f, a, 2);
    BlockRead(f, b, 2);
    close(f);

    writeln(a[0], ' ', a[1], ' ', b[0], ' ', b[1]);
end.
