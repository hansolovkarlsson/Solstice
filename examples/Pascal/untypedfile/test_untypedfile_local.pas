program TestUntypedfileLocal;

{ A LOCAL (procedure-scoped) array as the BlockRead/BlockWrite target -
  only the FILE variable is global-only, the array target isn't.
  99 98 97 }

var
    f: file;

procedure RoundTrip;
    var
        buf: array[0..2] of integer;
        i: integer;
begin
    buf[0] := 99;
    buf[1] := 98;
    buf[2] := 97;

    assign(f, '/tmp/untypedfile_local.bin');
    rewrite(f);
    BlockWrite(f, buf, 3);
    close(f);

    for i := 0 to 2 do
        buf[i] := 0;

    reset(f);
    BlockRead(f, buf, 3);
    close(f);

    for i := 0 to 2 do
        write(buf[i], ' ');
    writeln;
end;

begin
    RoundTrip;
end.
