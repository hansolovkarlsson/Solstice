program TestRandomRandomize;
var
    i, r: integer;
begin
    Randomize;
    for i := 1 to 100 do begin
        r := Random(100);
        assert((r >= 0) and (r < 100), 'Random(100) produced an out-of-range value');
    end;
    writeln('ok');
end.
