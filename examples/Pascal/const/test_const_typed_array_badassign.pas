program TestConstTypedArrBadAssign;
const
    Fib: array[1..3] of integer = (1, 1, 2);
begin
    Fib[1] := 9;
end.
