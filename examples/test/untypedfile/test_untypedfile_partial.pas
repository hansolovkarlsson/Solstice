program TestUntypedfilePartial;

{ BlockRead/BlockWrite a count SMALLER than the array's own declared
  size - confirms only the first 'count' elements are touched, the rest
  of the array stays whatever it was. 10 20 99 99 99 }

var
    f: file;
    buf: array[0..4] of integer;
    i: integer;

begin
    buf[0] := 10;
    buf[1] := 20;
    buf[2] := 30;
    buf[3] := 40;
    buf[4] := 50;

    assign(f, '/tmp/untypedfile_partial.bin');
    rewrite(f);
    BlockWrite(f, buf, 2);   { only writes 10, 20 }
    close(f);

    for i := 0 to 4 do
        buf[i] := 99;

    reset(f);
    BlockRead(f, buf, 2);    { only overwrites buf[0], buf[1] }
    close(f);

    for i := 0 to 4 do
        write(buf[i], ' ');
    writeln;
end.
