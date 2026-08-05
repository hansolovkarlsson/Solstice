program TestPtrNil;
{ nil: the default/reset value, equality/inequality against a real
  pointer and against itself, and re-pointing a variable back to nil.
  Expected output:
  p is nil
  p is not nil
  p is nil again
  same
  different }
type
    PInt = ^integer;
var
    p, q: PInt;
begin
    p := nil;
    if p = nil then writeln('p is nil');

    new(p);
    if p <> nil then writeln('p is not nil');

    dispose(p);
    p := nil;
    if p = nil then writeln('p is nil again');

    new(p);
    q := p;
    if p = q then writeln('same');

    new(q);
    if p <> q then writeln('different');
end.
