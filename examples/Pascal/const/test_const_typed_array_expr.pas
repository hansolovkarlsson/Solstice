program TestConstTypedArrayExpr;
const
    N = 10;
    Fib: array[1..3] of integer = (1, N, N * 2);
begin
    writeln(Fib[1], ' ', Fib[2], ' ', Fib[3]);
end.
