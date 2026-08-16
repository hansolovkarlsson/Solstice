program TestSizedintTypedfileCompat;
type
    TByte = 0..255;
    TRec = record
        flag: TByte;
        total: integer;
    end;
var
    f: file of TRec;
    r, r2: TRec;
begin
    assign(f, '/tmp/ouroboros_test_sizedint_typedfile_compat.bin');
    r.flag := 200; r.total := 999;
    rewrite(f);
    write(f, r);
    write(f, r);
    close(f);
    reset(f);
    writeln('records = ', filesize(f));
    read(f, r2);
    writeln(r2.flag, ' ', r2.total);
    close(f);
end.
