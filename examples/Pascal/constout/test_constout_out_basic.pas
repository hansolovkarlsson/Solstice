program TestConstoutOutBasic;
var
    a: integer;

procedure MakeIt(out y: integer);
begin
    y := 99;
end;

begin
    MakeIt(a);
    writeln(a);
end.
