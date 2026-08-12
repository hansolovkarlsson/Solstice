program TestSizedintTypedfileRecord;
type
    TRec = record
        flag: byte;
        delta: shortint;
        count: word;
        total: integer;
    end;
var
    f: file of TRec;
    r, r2: TRec;
begin
    assign(f, '/tmp/ouroboros_test_sizedint_typedfile_record.bin');
    r.flag := 200; r.delta := -5; r.count := 40000; r.total := 999;
    rewrite(f);
    write(f, r);
    r.flag := 1; r.delta := 1; r.count := 1; r.total := 1;
    write(f, r);
    close(f);

    reset(f);
    writeln('records = ', filesize(f));
    read(f, r2);
    writeln(r2.flag, ' ', r2.delta, ' ', r2.count, ' ', r2.total);
    seek(f, 1);
    read(f, r2);
    writeln(r2.flag, ' ', r2.delta, ' ', r2.count, ' ', r2.total);
    close(f);
end.
