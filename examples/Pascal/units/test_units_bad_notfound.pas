program TestUnitsBadNotfound;

// 'uses' naming a unit whose file doesn't exist - clean compile error,
// not a crash.

uses ThisUnitDoesNotExist;

begin
end.
