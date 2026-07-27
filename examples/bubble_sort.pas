program BubbleSort;
var
    nums: array[1..7] of integer;
    i, j, temp: integer;
begin
    nums[1] := 42;
    nums[2] := 17;
    nums[3] := 93;
    nums[4] := 8;
    nums[5] := 61;
    nums[6] := 29;
    nums[7] := 5;

    write('Before: ');
    for i := 1 to 7 do
        write(nums[i], ' ');
    writeln;

    for i := 1 to 6 do
        for j := 1 to 7 - i do
            if nums[j] > nums[j + 1] then begin
                temp := nums[j];
                nums[j] := nums[j + 1];
                nums[j + 1] := temp;
            end;

    write('After:  ');
    for i := 1 to 7 do
        write(nums[i], ' ');
    writeln;
end.
