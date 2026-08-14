program TestDynArrayCopySliceStartCount;
var
    a, b: array of integer;
    i: integer;
begin
    SetLength(a, 6);
    for i := 0 to High(a) do
        a[i] := i;

    b := Copy(a, 1, 3);        { 3 elements starting at index 1 }
    writeln(Length(b));         { 3 }
    for i := 0 to High(b) do
        write(b[i], ' ');
    writeln;                    { 1 2 3 }
end.
