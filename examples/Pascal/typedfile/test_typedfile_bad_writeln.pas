program TestTypedfileBadWriteln;
var
    f: file of integer;
    x: integer;
begin
    assign(f, '/tmp/ouroboros_test_typedfile_bad_writeln.bin');
    rewrite(f);
    writeln(f, x);
end.
