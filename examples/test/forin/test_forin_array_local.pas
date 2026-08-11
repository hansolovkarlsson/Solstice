program TestForInArrayLocal;
procedure Run;
var
    nums: array[1..3] of integer;
    x, i: integer;
begin
    for i := 1 to 3 do nums[i] := i * i;
    for x in nums do
        writeln(x);
end;
begin
    Run;
end.
