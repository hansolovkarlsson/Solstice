program TestDynArrayLiteralReal;
var
    arr: array of real;
    i: integer;
begin
    arr := [1.5, 2.5, 3.0];
    writeln(Length(arr));       { 3 }
    for i := 0 to High(arr) do
        write(arr[i]:0:1, ' ');
    writeln;                     { 1.5 2.5 3.0 }
end.
