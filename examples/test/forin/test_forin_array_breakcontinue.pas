program TestForInArrayBreakContinue;
var
    nums: array[1..5] of integer;
    x, i: integer;
begin
    for i := 1 to 5 do nums[i] := i;
    for x in nums do begin
        if x = 4 then break;
        if x mod 2 = 0 then continue;
        writeln(x);
    end;
end.
