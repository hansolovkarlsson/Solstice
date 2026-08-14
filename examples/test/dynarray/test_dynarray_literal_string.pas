program TestDynArrayLiteralString;
var
    arr: array of string;
    i: integer;
begin
    arr := ['hello', 'world', '!'];
    writeln(Length(arr));   { 3 }
    for i := 0 to High(arr) do
        write(arr[i], ' ');
    writeln;                 { hello world ! }
end.
