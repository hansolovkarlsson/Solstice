program TestProcParamNestedCaller;

function Triple(n: integer): integer;
begin
    Triple := n * 3;
end;

procedure Outer;
    var result: integer;

    function Apply(function f(n: integer): integer; v: integer): integer;
    begin
        Apply := f(v);
    end;

begin
    result := Apply(Triple, 7);
    writeln(result); { 21 }
end;

begin
    Outer;
end.
