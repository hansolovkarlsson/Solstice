program TestPtrBadoperator;
{ Standard Pascal defines only '=' and '<>' for pointers - no
  arithmetic, no ordering. Must be a Compile Error. }
type
    PInt = ^integer;
var
    p, q: PInt;
begin
    new(p);
    new(q);
    if p < q then writeln('unreachable');
end.
