program TestForInArrayBasic;
var
    nums: array[1..5] of integer;
    x, i: integer;
begin
    for i := 1 to 5 do nums[i] := i * 10;
    for x in nums do
        writeln(x);
end.
