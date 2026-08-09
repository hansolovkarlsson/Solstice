program TestUnitsBadCircular;

// CircularA uses CircularB, which uses CircularA again - a genuine
// cycle, not a diamond (compare test_units_diamond.pas). Clean compile
// error, not an infinite loop/crash.

uses CircularA;

begin
end.
