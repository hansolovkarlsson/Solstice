program TestFileioEnum;
type
    TColor = (Red, Green, Blue);
var
    f: text;
    line: string;
begin
    assign(f, '/tmp/ouroboros_test_fileio_enum.txt');
    rewrite(f);
    writeln(f, Green);
    close(f);

    assign(f, '/tmp/ouroboros_test_fileio_enum.txt');
    reset(f);
    readln(f, line);
    writeln('[', line, ']');
    close(f);
end.
