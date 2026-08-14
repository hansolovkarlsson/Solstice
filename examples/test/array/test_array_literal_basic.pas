program TestArrayLiteralBasic;
{ Array-literal syntax for a FIXED-size array, mirroring what dynamic
  arrays already had - 'arr := [1, 2, 3];' instead of an element-by-
  element indexed write. See docs/LANGUAGE.md#array-literals. }
var
    arr: array[1..3] of integer;
    i: integer;
begin
    arr := [10, 20, 30];
    for i := 1 to 3 do writeln(arr[i]);
end.
