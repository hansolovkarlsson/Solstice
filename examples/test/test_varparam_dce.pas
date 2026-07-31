program TestVarParamDCE;
var
    g: integer;

{ g's ONLY use anywhere is being passed as a 'var' argument here - if
  dead-code elimination didn't know NODE_VAR_REF counts as a genuine use
  (matching how NODE_ARRAY_REF already does), 'g := 77;' below would be
  wrongly stripped as dead, and this would print 0 instead of 77. }
procedure ReadIt(var x: integer);
begin
    writeln(x);
end;

begin
    g := 77;
    ReadIt(g);
end.
