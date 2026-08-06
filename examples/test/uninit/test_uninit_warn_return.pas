program TestUninitWarnReturn;
{ Expected: a compile-time Warning (not an error) that function F never
  assigns a value to its own name. }
function F: integer;
begin
    writeln('doing nothing useful');
end;

begin
    writeln(F);
end.
