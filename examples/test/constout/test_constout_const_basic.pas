program TestConstoutConstBasic;
var
    n: integer;

procedure ShowIt(const x: integer);
begin
    writeln(x);
end;

begin
    n := 5;
    ShowIt(n);
end.
