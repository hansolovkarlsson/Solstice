program DceIf2;
var
    truly_dead: integer;
    y: integer;
begin
    y := 1;
    if y > 0 then
        truly_dead := 99
    else
        truly_dead := 42;
    writeln(y);
end.
