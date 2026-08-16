program TestSizeofTypedfileVar;
type
    TRec = record
        flag: byte;
        total: integer;
    end;
var
    f: file of TRec;
begin
    assign(f, '/tmp/ouroboros_test_sizeof_typedfile_var.bin');
    { works before reset/rewrite ever opens the file - a pure
      declaration-time answer, unlike filesize(f) }
    writeln(sizeOf(f));
    writeln(sizeOf(TRec));
end.
