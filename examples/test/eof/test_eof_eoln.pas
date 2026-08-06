program TestEofEoln;
var
    x, total: integer;
begin
    { eoln mid-line: not yet at end of line right after the first of two
      values on one line, but is after the second (the trailing newline) }
    read(x);
    if eoln then
        writeln('at eoln too soon')
    else
        writeln('correctly not at eoln yet');
    read(x);
    if eoln then
        writeln('correctly at eoln')
    else
        writeln('incorrectly not at eoln');

    { eof: keeps reading lines until none are left }
    total := 0;
    while not eof do begin
        readln(x);
        total := total + x;
    end;
    writeln('total=', total);
end.
