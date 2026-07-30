program RepeatTest;
var
    n: integer;
begin
    n := 0;
    repeat
        n := n + 1;
        writeln(n);
    until n = 3;
end.
