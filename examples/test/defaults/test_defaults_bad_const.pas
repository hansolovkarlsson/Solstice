program TestDefaultsBadConst;
{ 'const' parameters in this compiler are by-reference (see
  docs/LANGUAGE.md's const-parameters section) - a default has no
  caller-side lvalue to reference, so defaults are rejected on 'const'
  too, not just 'var'/'out' }
procedure P(const x: integer = 5);
begin
end;

begin
end.
