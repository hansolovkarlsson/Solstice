program ArrParamExpr;
var nums: array[1..5] of integer;
function sumArray(arr: array[1..5] of integer): integer;
begin
    sumArray := 0;
end;
begin
    writeln(sumArray(nums[1]));
end.
