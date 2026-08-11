program TestForInArrayBadTypeMismatch;
var
    nums: array[1..3] of integer;
    b: boolean;
begin
    for b in nums do
        writeln(b);
end.
