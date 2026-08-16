program TestArrayLiteralBadCountMany;
{ An array literal must supply exactly the array's own element count -
  too many is a compile-time error. }
var
    arr: array[1..3] of integer;
begin
    arr := [1, 2, 3, 4];
end.
