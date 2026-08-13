program TestConstTypedArraySub;
const
    Small: array[1..3] of byte = (1, 2, 255);
var
    i: integer;
begin
    for i := 1 to 3 do
        writeln(Small[i]);
end.
