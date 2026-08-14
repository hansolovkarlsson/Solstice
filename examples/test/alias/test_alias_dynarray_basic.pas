program TestAliasDynArrayBasic;
type
    TIntArray = array of integer;
var
    arr: TIntArray;
    i: integer;
begin
    SetLength(arr, 3);
    for i := 0 to High(arr) do
        arr[i] := i * i;
    for i := 0 to High(arr) do
        write(arr[i], ' ');
    writeln;                { 0 1 4 }
    writeln(Length(arr));   { 3 }
    arr := [10, 20, 30];    { array-literal assignment through the alias }
    writeln(arr[0], ' ', arr[1], ' ', arr[2]);  { 10 20 30 }
end.
