program TV1;
var
    a, b, c: integer;
    ok: boolean;
begin
    a := 7;
    b := 3;
    c := a * b - (a div b) + (a mod b);
    ok := (a > b) and (c >= 0);
    writeln(c);
    writeln(ok);
end.
