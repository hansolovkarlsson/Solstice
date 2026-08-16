program TestConstoutForwardConstToConst;
var
    n: integer;

procedure Inner(const v: integer);
begin
    writeln(v);
end;

procedure Outer(const c: integer);
begin
    Inner(c);
end;

begin
    n := 7;
    Outer(n);
end.
