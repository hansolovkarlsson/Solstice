program TestTypedfileSeek;
type
    TRecord = record
        id: integer;
    end;
var
    f: file of TRecord;
    r: TRecord;
    i: integer;
begin
    assign(f, '/tmp/ouroboros_test_typedfile_seek.bin');
    rewrite(f);
    for i := 0 to 4 do begin
        r.id := i * 100;
        write(f, r);
    end;
    close(f);

    reset(f);
    seek(f, 3);
    read(f, r);
    writeln('record at seek(3): id=', r.id);
    seek(f, 0);
    read(f, r);
    writeln('record at seek(0): id=', r.id);
    close(f);
end.
