program TestDArrTCBadReassign;
{ A BARE dynamic-array typed constant can't be reassigned. }
const
    X: array of integer = [1, 2];
begin
    X := [3, 4];
end.
