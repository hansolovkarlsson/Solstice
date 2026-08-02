program TestFileioLocalTarget;
var
    f: text;

procedure ReadIt;
var
    x: integer;
    s: string;
begin
    readln(f, x);
    readln(f, s);
    writeln('x=', x, ' s=', s);
end;

begin
    assign(f, '/tmp/ouroboros_test_fileio_local.txt');
    rewrite(f);
    writeln(f, 7);
    writeln(f, 'hi there');
    close(f);

    assign(f, '/tmp/ouroboros_test_fileio_local.txt');
    reset(f);
    ReadIt;
    close(f);
end.
