program TestDynArrayLiteralChar;
var
    arr: array of char;
    i: integer;
begin
    arr := ['a', 'b', 'c'];
    writeln(Length(arr));   { 3 }
    for i := 0 to High(arr) do
        write(arr[i]);
    writeln;                 { abc }
end.
