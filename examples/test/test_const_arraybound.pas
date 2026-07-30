program TestConstArrayBound;
const
    N = 5;
    Lo = -2;
var
    arr: array[1..N] of integer;
    grid: array[Lo..N, 1..N] of integer;
    i, j: integer;
begin
    for i := 1 to N do
        arr[i] := i * i;
    writeln('arr[N] = ', arr[N]);
    writeln('low = ', low(arr), ' high = ', high(arr), ' length = ', length(arr));

    for i := Lo to N do
        for j := 1 to N do
            grid[i, j] := i + j;
    writeln('grid[Lo,N] = ', grid[Lo, N]);
end.
