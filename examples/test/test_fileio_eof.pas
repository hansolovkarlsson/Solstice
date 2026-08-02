program TestFileioEof;
var
    f: text;
    line: string;
begin
    assign(f, '/tmp/ouroboros_test_fileio_eof.txt');
    rewrite(f);
    writeln(f, 'one');
    writeln(f, 'two');
    writeln(f, 'three');
    close(f);

    assign(f, '/tmp/ouroboros_test_fileio_eof.txt');
    reset(f);
    while not eof(f) do begin
        readln(f, line);
        writeln('> ', line);
    end;
    close(f);
end.
