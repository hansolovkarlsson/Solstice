program TestNdArrayBadBounds;
var
    a: array[1..3, 1..3, 1..3] of integer;
procedure P(var c: array[1..2, 1..2, 1..2] of integer);
begin
end;
begin
    P(a);
end.
