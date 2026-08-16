program TestClassCompositeBadWhole;
type
    TPoint = record
        x, y: integer;
    end;
    TCircle = class
        center: TPoint;
    end;
var
    c: TCircle;
begin
    new(c);
    { 'center' names a whole nested record - reading/writing/passing it
      as a unit isn't supported yet (a known gap); must specify a
      further field. Expected: Compile Error. }
    writeln(c.center);
    dispose(c);
end.
