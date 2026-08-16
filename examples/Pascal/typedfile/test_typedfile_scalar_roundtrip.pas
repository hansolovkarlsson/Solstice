program TestTypedfileScalar;
var
    f: file of integer;
    x: integer;
begin
    assign(f, '/tmp/ouroboros_test_typedfile_scalar.bin');
    rewrite(f);
    x := 7;
    write(f, x);
    x := 21;
    write(f, x);
    close(f);

    reset(f);
    read(f, x);
    writeln('first: ', x);
    read(f, x);
    writeln('second: ', x);
    close(f);
end.
