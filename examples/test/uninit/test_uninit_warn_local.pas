program TestUninitWarnLocal;
{ Expected: a compile-time Warning (not an error - compilation still
  succeeds and the program still runs) that 'x' is read but never
  assigned a value in procedure P. }
procedure P;
var
    x: integer;
begin
    writeln(x);
end;

begin
    P;
end.
