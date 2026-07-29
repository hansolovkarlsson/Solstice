program ProcDce;
var
    shared: integer;

procedure setShared;
begin
    shared := 99;   { must NOT be eliminated as dead - it's read in main }
end;

begin
    setShared;
    writeln(shared);
end.
