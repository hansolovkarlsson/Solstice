program Nested;
var
    i: integer;
begin
    i := 1;
    while i <= 5 do begin
        if i mod 2 = 0 then
            writeln(i)
        else begin
            writeln(0);
        end;
        i := i + 1;
    end;
end.
