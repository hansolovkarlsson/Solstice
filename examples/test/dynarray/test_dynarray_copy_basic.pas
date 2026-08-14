program TestDynArrayCopyBasic;
var
    a, b: array of integer;
    i: integer;
begin
    SetLength(a, 4);
    for i := 0 to High(a) do
        a[i] := i * 10;

    b := Copy(a);
    writeln(Length(b));      { 4 }
    for i := 0 to High(b) do
        write(b[i], ' ');
    writeln;                  { 0 10 20 30 }

    { Not aliased: mutating the copy must not affect the original. }
    b[0] := 999;
    writeln(a[0]);            { 0 }
    writeln(b[0]);            { 999 }
end.
