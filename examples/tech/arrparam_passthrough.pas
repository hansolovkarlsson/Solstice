program ArrParamPassthrough;
var nums: array[1..3] of integer;

function first(arr: array[1..3] of integer): integer;
begin
    first := arr[1];
end;

function relay(arr: array[1..3] of integer): integer;
begin
    relay := first(arr);   { forwards ITS OWN array parameter }
end;

begin
    nums[1] := 42;
    nums[2] := 0;
    nums[3] := 0;
    writeln(relay(nums));
end.
