program TestUntypedfileBasic;

{ Write an array's worth of integers via BlockWrite, close, reopen,
  BlockRead them back, compare. 10 20 30 }

var
    f: file;
    buf: array[0..2] of integer;
    i: integer;

begin
    buf[0] := 10;
    buf[1] := 20;
    buf[2] := 30;

    assign(f, '/tmp/untypedfile_basic.bin');
    rewrite(f);
    BlockWrite(f, buf, 3);
    close(f);

    buf[0] := 0;
    buf[1] := 0;
    buf[2] := 0;

    reset(f);
    BlockRead(f, buf, 3);
    close(f);

    for i := 0 to 2 do
        write(buf[i], ' ');
    writeln;
end.
