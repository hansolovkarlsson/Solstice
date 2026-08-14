program TestUntypedfileEof;

{ eof(f) after reading everything. before=false after=true }

var
    f: file;
    buf: array[0..1] of integer;

begin
    buf[0] := 1;
    buf[1] := 2;
    assign(f, '/tmp/untypedfile_eof.bin');
    rewrite(f);
    BlockWrite(f, buf, 2);
    close(f);

    reset(f);
    write('before=');
    if eof(f) then write('true ') else write('false ');
    BlockRead(f, buf, 2);
    write('after=');
    if eof(f) then writeln('true') else writeln('false');
    close(f);
end.
