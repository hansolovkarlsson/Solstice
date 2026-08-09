program TestUnitsClass;

// A class declared in a unit's interface (fields + method header), with
// its method body in the unit's implementation - see Shapes.pas. Two
// instances confirm each has its own 'radius': Area = radius*radius*3,
// so 5 -> 75 and 2 -> 12.

uses Shapes;

var
  a, b: TCircle;

begin
  new(a);
  new(b);
  a.radius := 5;
  b.radius := 2;
  writeln(a.Area);
  writeln(b.Area);
  dispose(a);
  dispose(b);
end.
