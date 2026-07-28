program ProcDce2;
var
    used: integer;
    dead: integer;

procedure doWork;
begin
    used := 5;
    dead := 10;   { never read anywhere - should be eliminated }
end;

begin
    doWork;
    writeln(used);
end.
