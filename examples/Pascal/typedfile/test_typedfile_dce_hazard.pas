program TestTypedfileDceHazard;
type
    TRecord = record
        id: integer;
    end;
var
    f: file of TRecord;
    rec1, rec2: TRecord;
begin
    assign(f, '/tmp/ouroboros_test_typedfile_dce.bin');
    rewrite(f);
    rec1.id := 111;
    write(f, rec1);
    rec1.id := 222;
    write(f, rec1);
    close(f);

    reset(f);
    read(f, rec1);
    { rec1 is never read after this - if the optimizer's dead-code
      elimination wrongly eliminated the read above (missing its
      file-position-advancing side effect), this second read would
      incorrectly get the FIRST record's data (111) instead of the
      second record's (222), since the file position wouldn't have
      advanced past the first read at all. }
    read(f, rec2);
    writeln('rec2.id = ', rec2.id);
    close(f);
end.
