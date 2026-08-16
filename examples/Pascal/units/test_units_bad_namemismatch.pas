program TestUnitsBadNamemismatch;

// MismatchedUnit.pas declares 'unit SomeOtherName;' instead of
// 'unit MismatchedUnit;' - the filename and the declared unit name must
// match. Clean compile error, not a crash.

uses MismatchedUnit;

begin
end.
