program TestProcParamBadVarness;

var x: integer;

procedure Plain(n: integer);
begin
end;

procedure ApplyVar(procedure p(var n: integer); target: integer);
begin
    p(target);
end;

begin
    x := 5;
    ApplyVar(Plain, x); { Plain's param isn't 'var', p's is }
end.
