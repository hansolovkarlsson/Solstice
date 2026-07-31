program TestVarParamRecursion;
var
    total: integer;

{ passes its OWN 'var' parameter straight back into a recursive call -
  every level must still resolve to the same original global }
procedure CountUp(var n: integer; limit: integer);
begin
    if n < limit then begin
        n := n + 1;
        CountUp(n, limit);
    end;
end;

begin
    total := 0;
    CountUp(total, 5);
    writeln('total=', total);
end.
