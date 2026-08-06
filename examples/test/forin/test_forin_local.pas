program TestForInLocal;

procedure Show(s: set of 0..9);
var
    x: integer;
begin
    for x in s do
        writeln(x);
end;

begin
    Show([2, 4, 6]);
end.
