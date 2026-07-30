program DceIf;
var
    used_in_if: integer;
    y: integer;
begin
    used_in_if := 10;
    y := 1;
    if used_in_if > 5 then
        y := 2
    else
        y := 3;
    writeln(y);
end.
