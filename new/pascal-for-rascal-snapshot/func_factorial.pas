program FuncFactorial;
var
    i: integer;

function factorial(n: integer): integer;
begin
    if n <= 1 then
        factorial := 1
    else
        factorial := n * factorial(n - 1);
end;

begin
    for i := 1 to 6 do
        writeln(i, '! = ', factorial(i));
end.
