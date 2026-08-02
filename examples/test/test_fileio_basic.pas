program TestFileioBasic;
var
    f: text;
    line: string;
    n: integer;
begin
    assign(f, '/tmp/ouroboros_test_fileio_basic.txt');
    rewrite(f);
    writeln(f, 'Hello, file!');
    writeln(f, 42);
    write(f, 'no newline');
    close(f);

    assign(f, '/tmp/ouroboros_test_fileio_basic.txt');
    reset(f);
    readln(f, line);
    writeln('Line 1: ', line);
    readln(f, n);
    writeln('Line 2 (int): ', n);
    readln(f, line);
    writeln('Line 3: ', line);
    close(f);
end.
