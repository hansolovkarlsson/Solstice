program ArrayTest;
var
    nums: array[1..5] of integer;
    i, sum: integer;
    flags: array[0..3] of boolean;
    names: array[1..2] of string;
begin
    for i := 1 to 5 do
        nums[i] := i * i;

    for i := 1 to 5 do
        writeln(nums[i]);

    sum := 0;
    for i := 1 to 5 do
        sum := sum + nums[i];
    writeln('sum=', sum);

    flags[0] := true;
    flags[1] := false;
    if flags[0] then
        writeln('flags[0] is true');

    names[1] := 'Alice';
    names[2] := 'Bob';
    writeln(names[1], ' and ', names[2]);
end.
