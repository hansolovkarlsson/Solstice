program TestLowHighLocal;
var g: array[1..5] of integer;
    i: integer;

function sumArray(arr: array[1..5] of integer): integer;
var
    local_data: array[10..12] of integer;
    s: integer;
begin
    writeln('param low/high/length: ', low(arr), ' ', high(arr), ' ', length(arr));
    writeln('local low/high/length: ', low(local_data), ' ', high(local_data), ' ', length(local_data));
    s := 0;
    for i := low(arr) to high(arr) do
        s := s + arr[i];
    sumArray := s;
end;

begin
    g[1] := 1; g[2] := 2; g[3] := 3; g[4] := 4; g[5] := 5;
    writeln('sum = ', sumArray(g));
end.
