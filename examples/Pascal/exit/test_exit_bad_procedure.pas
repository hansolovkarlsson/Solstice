program TestExitBadProcedure;
{ 'exit(value);' is only legal inside a FUNCTION - a plain procedure has
  no return_slot to assign into. Expected: compile error. }
procedure Foo;
begin
    exit(5);
end;
begin
    Foo;
end.
