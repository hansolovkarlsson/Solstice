program TestProcParamBasic;

function Square(n: integer): integer;
begin
    Square := n * n;
end;

function Cube(n: integer): integer;
begin
    Cube := n * n * n;
end;

function Apply(function f(n: integer): integer; v: integer): integer;
begin
    Apply := f(v);
end;

begin
    writeln(Apply(Square, 5));
    writeln(Apply(Cube, 3));
end.
