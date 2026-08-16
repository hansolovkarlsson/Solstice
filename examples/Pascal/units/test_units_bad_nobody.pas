program TestUnitsBadNobody;

// NoBodyUnit's interface declares 'NeverImplemented' but its
// implementation section never completes it - the same pre-existing
// end-of-parse "forward-declared but never defined" check other forward
// declarations already hit, exercised here in the units context. Clean
// compile error, not a crash.

uses NoBodyUnit;

begin
end.
