program TestDynArrayLiteralLocal;

procedure DoIt;
var
    arr: array of integer;
    i: integer;
begin
    arr := [7, 8, 9];
    writeln(Length(arr));
    for i := 0 to High(arr) do
        write(arr[i], ' ');
    writeln;
end;

begin
    DoIt;
end.
