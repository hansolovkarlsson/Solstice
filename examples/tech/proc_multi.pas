program ProcMulti;
var
    total: integer;

procedure resetTotal;
begin
    total := 0;
end;

procedure addOne;
begin
    resetTotal;    { calling an earlier-declared procedure }
    total := total + 1;
end;

begin
    addOne;
    writeln(total);
end.
