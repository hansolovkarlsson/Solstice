program DCETest;
var
    x, unused_after_writeln, y: integer;
begin
    x := 5;
    writeln(x);
    unused_after_writeln := 99;
    y := 1;
    writeln(y);
end.
