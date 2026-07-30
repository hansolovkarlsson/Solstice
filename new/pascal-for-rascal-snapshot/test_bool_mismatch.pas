program TestBoolMismatch;
var a: boolean; x: integer;
begin
    a := true;
    x := 5;
    if a = x then writeln('bad');
end.
