program TestProcParamBadCount;

function AddTwo(a, b: integer): integer;
begin
    AddTwo := a + b;
end;

function Apply(function f(n: integer): integer; v: integer): integer;
begin
    Apply := f(v);
end;

begin
    writeln(Apply(AddTwo, 5)); { AddTwo takes 2 params, f expects 1 }
end.
