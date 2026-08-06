program TestProcParamVarParam;

var
    a: integer;

procedure DoubleIt(var x: integer);
begin
    x := x * 2;
end;

procedure TripleIt(var x: integer);
begin
    x := x * 3;
end;

procedure ApplyInPlace(procedure p(var x: integer); var target: integer);
begin
    p(target);
end;

begin
    a := 5;
    ApplyInPlace(DoubleIt, a);
    writeln(a); { 10 }
    ApplyInPlace(TripleIt, a);
    writeln(a); { 30 }
end.
