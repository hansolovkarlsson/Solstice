program TestScalarReturnUnaffected;
{ Confirms the new "read the function's own name mid-body" support added
  for a DYNAMIC-ARRAY return type is gated correctly and does NOT affect
  a scalar return type: reading a scalar function's own name mid-body is
  still, deliberately, a recursive call (see docs/LANGUAGE.md#functions),
  so this must still overflow the call stack, not read back 5. }
function Foo: integer;
begin
    Foo := 5;
    Foo := Foo + 1;
end;
begin
    writeln(Foo);
end.
