program TestConstTypedArrayProc;
const
    Fib: array[1..5] of integer = (1, 1, 2, 3, 5);

procedure PrintFib;
var
    i: integer;
begin
    for i := 1 to 5 do
        writeln(Fib[i]);
end;

begin
    { confirms the typed constant's initializer runs before ANY
      procedure call that reads it }
    PrintFib;
end.
