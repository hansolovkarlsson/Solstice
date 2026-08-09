program TestUnitsBasic;

// Exercises a unit exporting a const, a var, a procedure, and a
// function - see MathUtils.pas. Square(4) = 4*4*Factor = 4*4*3 = 48.
// CallCount is bumped once by Square and once by Bump, so ends at 2.

uses MathUtils;

var
  r: integer;

begin
  r := Square(4);
  Bump;
  writeln(r);
  writeln(CallCount);
end.
