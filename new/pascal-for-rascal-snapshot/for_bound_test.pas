program ForBoundTest;
var
    i, n, count: integer;
begin
    n := 5;
    count := 0;
    for i := 1 to n do begin
        count := count + 1;
        n := 0;   { if the end bound were re-evaluated each iteration, this would stop the loop immediately }
    end;
    writeln(count);  { must be 5, not 1 }
end.
