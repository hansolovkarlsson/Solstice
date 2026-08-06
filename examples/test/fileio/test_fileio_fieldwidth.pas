program TestFileioFieldWidth;
var
    f: text;
    line: string;
begin
    assign(f, '/tmp/ouroboros_test_fileio_width.txt');
    rewrite(f);
    writeln(f, 42:6);
    writeln(f, 3.14159:8:2);
    close(f);

    assign(f, '/tmp/ouroboros_test_fileio_width.txt');
    reset(f);
    readln(f, line);
    writeln('[', line, ']');
    readln(f, line);
    writeln('[', line, ']');
    close(f);
end.
