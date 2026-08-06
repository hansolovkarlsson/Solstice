program TestProcParamBadNested;

procedure Outer;
    function Inner(n: integer): integer;
    begin
        Inner := n;
    end;
    function Apply(function f(n: integer): integer; v: integer): integer;
    begin
        Apply := f(v);
    end;
begin
    writeln(Apply(Inner, 5)); { should be rejected - Inner is nested }
end;

begin
    Outer;
end.
