program TestProcParamBadExpr;

procedure Silent(n: integer);
begin
end;

function UseIt(procedure p(n: integer); v: integer): integer;
begin
    UseIt := p(v) + 1; { p is a procedure - can't be used in an expression }
end;

begin
    writeln(UseIt(Silent, 5));
end.
