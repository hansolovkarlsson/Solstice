program TestSetBadLt;
var
    a, b: set of 0..9;
begin
    a := [1];
    b := [2];
    writeln(a < b);
end.
