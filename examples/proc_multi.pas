program ProcMulti;
var
    total: integer;

procedure reset;
begin
    total := 0;
end;

procedure addOne;
begin
    reset;         { calling an earlier-declared procedure }
    total := total + 1;
end;

begin
    addOne;
    writeln(total);
end.
