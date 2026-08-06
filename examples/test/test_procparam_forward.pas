program TestProcParamForward;

function Negate(n: integer): integer; forward;

function ApplyToFive(function f(n: integer): integer): integer;
begin
    ApplyToFive := f(5);
end;

function Negate;
begin
    Negate := -n;
end;

begin
    writeln(ApplyToFive(Negate)); { -5 }
end.
