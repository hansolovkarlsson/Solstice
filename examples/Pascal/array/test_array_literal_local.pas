program TestArrayLiteralLocal;
{ Same as test_array_literal_basic.pas, but the array is a procedure
  LOCAL (a mangled global underneath - add_local_array() - so this
  exercises a separate parse site from the global case, sharing the
  same parse_fixed_array_literal_assign() helper). }
procedure Foo;
var
    arr: array[0..2] of integer;
    i: integer;
begin
    arr := [1, 2, 3];
    for i := 0 to 2 do writeln(arr[i]);
end;
begin
    Foo;
end.
