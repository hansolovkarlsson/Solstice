program FuncForward;
var i: integer;

function fib(n: integer): integer; forward;

function fibHelper(n: integer): integer;
begin
    if n <= 1 then
        fibHelper := n
    else
        fibHelper := fib(n - 1) + fib(n - 2);
end;

function fib;
begin
    fib := fibHelper(n);
end;

begin
    for i := 0 to 9 do
        write(fib(i), ' ');
    writeln;
end.
