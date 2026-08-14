program TestArrayLiteralMixed;
{ A whole-array-literal assignment followed by an ordinary indexed
  write - confirms the literal assignment doesn't disturb the array's
  ordinary indexed read/write path afterward. }
var
    arr: array[1..3] of integer;
begin
    arr := [1, 2, 3];
    arr[2] := 99;
    writeln(arr[1], ' ', arr[2], ' ', arr[3]);   { 1 99 3 }
end.
