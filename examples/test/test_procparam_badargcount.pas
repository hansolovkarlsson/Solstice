program TestProcParamBadArgCount;

function Square(n: integer): integer;
begin
    Square := n * n;
end;

function Apply(function f(n: integer): integer; v: integer): integer;
begin
    Apply := f(v, v); { f only takes one argument }
end;

begin
    writeln(Apply(Square, 5));
end.
