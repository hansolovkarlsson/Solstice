program TestUnitsDiamond;

// DiamondA and DiamondB both 'uses Base' - Base must be merged exactly
// once (not re-declared / not a duplicate-declaration error) no matter
// how many units along the way depend on it. GetA = 10+1 = 11,
// GetB = 10+2 = 12.

uses DiamondA, DiamondB;

begin
  writeln(GetA);
  writeln(GetB);
end.
