program TestRealVerbose;
var
    pi: real;
    vals: array[1..3] of real;
    i: integer;
    total: real;
begin
    pi := 3.14159;
    vals[1] := 1.1;
    vals[2] := 2.2;
    vals[3] := 3.3;
    total := pi;
    for i := 1 to 3 do
        total := total + vals[i];
    writeln('total (forces everything to be read) = ', total);
end.
