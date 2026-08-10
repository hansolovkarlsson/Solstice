program TestTypedfileFilesize;
type
    TRecord = record
        id: integer;
    end;
var
    f: file of TRecord;
    r: TRecord;
    i: integer;
begin
    assign(f, '/tmp/ouroboros_test_typedfile_filesize.bin');
    rewrite(f);
    for i := 1 to 7 do begin
        r.id := i;
        write(f, r);
    end;
    close(f);

    reset(f);
    writeln('filesize: ', filesize(f));
    close(f);
end.
