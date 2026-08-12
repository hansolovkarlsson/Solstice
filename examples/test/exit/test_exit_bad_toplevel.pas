program TestExitBadToplevel;
{ 'exit(value);' at top level (outside any function) is also rejected -
  the main program has no return value to set either. Expected: compile
  error. }
begin
    exit(5);
end.
