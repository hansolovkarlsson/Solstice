program TestBoolArrayPrint;
var flags: array[1..3] of boolean; i: integer;
begin
    flags[1] := true;
    flags[2] := false;
    flags[3] := true;
    for i := 1 to 3 do
        write(flags[i], ' ');
    writeln;
end.
