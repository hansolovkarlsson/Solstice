program TestForinDynarrayBasic;

{ Sum every element of a global dynamic array. 60 }

var
    arr: array of integer;
    x, sum: integer;

begin
    SetLength(arr, 3);
    arr[0] := 10;
    arr[1] := 20;
    arr[2] := 30;
    sum := 0;
    for x in arr do
        sum := sum + x;
    writeln(sum);
end.
