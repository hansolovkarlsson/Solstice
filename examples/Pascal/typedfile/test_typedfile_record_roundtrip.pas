program TestTypedfileRoundtrip;
type
    TRecord = record
        id: integer;
        score: real;
    end;
var
    f: file of TRecord;
    r, r2: TRecord;
begin
    assign(f, '/tmp/ouroboros_test_typedfile_roundtrip.bin');
    rewrite(f);
    r.id := 42;
    r.score := 3.14;
    write(f, r);
    r.id := 99;
    r.score := 2.71;
    write(f, r);
    close(f);

    reset(f);
    read(f, r2);
    writeln('record 1: id=', r2.id, ' score=', r2.score:0:2);
    read(f, r2);
    writeln('record 2: id=', r2.id, ' score=', r2.score:0:2);
    close(f);
end.
