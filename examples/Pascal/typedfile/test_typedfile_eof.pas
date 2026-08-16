program TestTypedfileEof;
type
    TRecord = record
        id: integer;
    end;
var
    f: file of TRecord;
    r: TRecord;
    i: integer;
begin
    assign(f, '/tmp/ouroboros_test_typedfile_eof.bin');
    rewrite(f);
    for i := 1 to 3 do begin
        r.id := i;
        write(f, r);
    end;
    close(f);

    reset(f);
    read(f, r);
    writeln('after 1st read, eof: ', eof(f));
    read(f, r);
    writeln('after 2nd read, eof: ', eof(f));
    read(f, r);
    writeln('after 3rd (last) read, eof: ', eof(f));
    close(f);
end.
