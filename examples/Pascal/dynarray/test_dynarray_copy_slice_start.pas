program TestDynArrayCopySliceStart;
var
    a, b: array of integer;
    i: integer;
begin
    SetLength(a, 5);
    for i := 0 to High(a) do
        a[i] := i;

    b := Copy(a, 2);           { elements from index 2 to the end }
    writeln(Length(b));         { 3 }
    for i := 0 to High(b) do
        write(b[i], ' ');
    writeln;                    { 2 3 4 }
end.
