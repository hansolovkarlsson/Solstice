program TestConstTypedArrayInt;
const
    Fib: array[1..5] of integer = (1, 1, 2, 3, 5);
var
    i: integer;
begin
    for i := 1 to 5 do
        writeln(Fib[i]);
end.
