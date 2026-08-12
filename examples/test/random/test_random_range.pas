program TestRandomRange;
var
    i, r: integer;
begin
    Randomize;
    for i := 1 to 1000 do begin
        r := Random(6);
        assert((r >= 0) and (r < 6), 'Random(6) produced an out-of-range value');
    end;
    writeln('ok');
end.
