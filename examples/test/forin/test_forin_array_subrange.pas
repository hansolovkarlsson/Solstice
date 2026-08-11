program TestForInArraySubrange;
type
    TRange = 1..100;
var
    nums: array[1..3] of integer;
    x: TRange;
    i: integer;
begin
    for i := 1 to 3 do nums[i] := i * 20;
    for x in nums do
        writeln(x);
end.
