program TestUnitsBadDuplicate;

// DupUnit declares a global 'Total'; the main program declares its own
// 'Total' too - the same pre-existing duplicate-declaration check every
// other global name collision already hits (add_var()), exercised here
// across a unit boundary since there's no visibility enforcement yet
// (see docs/LANGUAGE.md#units). Clean compile error, not a crash.

uses DupUnit;

var
  Total: integer;

begin
end.
