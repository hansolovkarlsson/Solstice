program TestConsolidationDce;
type TCounter = record n: integer; end;
var used, dead: TCounter;
begin
    used.n := 5;
    inc(used.n);
    dead.n := 99;
    inc(dead.n);
    writeln(used.n);
end.
