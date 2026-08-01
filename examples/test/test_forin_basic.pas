program TestForInBasic;
var
    s: set of 0..9;
    x: integer;
begin
    s := [1, 3, 5, 9];
    for x in s do
        writeln(x);
end.
