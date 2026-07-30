program ArrParamBasic;
var
    nums: array[1..5] of integer;
    total: integer;
    i: integer;

function sumArray(arr: array[1..5] of integer): integer;
var
    s, k: integer;
begin
    s := 0;
    k := 1;
    while k <= 5 do begin
        s := s + arr[k];
        k := k + 1;
    end;
    sumArray := s;
end;

procedure doubleArray(arr: array[1..5] of integer);
var k: integer;
begin
    k := 1;
    while k <= 5 do begin
        arr[k] := arr[k] * 2;
        k := k + 1;
    end;
end;

begin
    for i := 1 to 5 do
        nums[i] := i;

    total := sumArray(nums);
    writeln('sum 1..5 = ', total);

    doubleArray(nums);
    for i := 1 to 5 do
        write(nums[i], ' ');
    writeln;
end.
