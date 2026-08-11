program TestDefaultsBadProctype;
function Square(n: integer): integer;
begin
    Square := n * n;
end;

function Apply(function f(n: integer): integer = Square; v: integer): integer;
begin
    Apply := f(v);
end;

begin
end.
