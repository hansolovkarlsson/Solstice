program TestLiteralArgSetUnaffected;
{ Confirms a SET-typed parameter's own '[...]' argument is completely
  unaffected by the new dynamic-array call-argument literal support -
  the dispatch is gated on the callee's declared parameter type, so a
  set parameter still gets an ordinary set constructor. }
type
    TDigits = set of 0..9;
procedure ShowSet(s: TDigits);
begin
    writeln(3 in s, ' ', 7 in s);
end;
begin
    ShowSet([3, 5]);  { TRUE FALSE }
end.
