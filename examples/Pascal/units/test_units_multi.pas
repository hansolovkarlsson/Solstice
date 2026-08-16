program TestUnitsMulti;

// Two independent, unrelated units named in one 'uses' clause.
// DoubleIt(5) = 10, TripleIt(5) = 15.

uses UnitOne, UnitTwo;

begin
  writeln(DoubleIt(5));
  writeln(TripleIt(5));
end.
