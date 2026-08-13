program TestDynArrayAlias;
var
    a, b: array of integer;
begin
    SetLength(a, 3);
    a[0] := 1; a[1] := 2; a[2] := 3;
    b := a;
    b[0] := 99;
    writeln(a[0], ' ', b[0]);
    writeln(Length(a), ' ', Length(b));
end.
