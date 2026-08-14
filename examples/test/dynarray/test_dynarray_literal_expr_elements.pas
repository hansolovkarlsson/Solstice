program TestDynArrayLiteralExprElements;
var
    arr: array of integer;
    x, y: integer;

function double_it(n: integer): integer;
begin
    double_it := n * 2;
end;

begin
    x := 5;
    y := 10;
    arr := [x, y + 1, double_it(3)];
    writeln(Length(arr));   { 3 }
    write(arr[0], ' ', arr[1], ' ', arr[2]);
    writeln;                 { 5 11 6 }
end.
