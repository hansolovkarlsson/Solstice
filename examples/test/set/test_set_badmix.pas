program TestSetBadMix;
var
    s: set of 0..9;
    x: integer;
begin
    x := 5;
    s := [1, 2] + x;
end.
