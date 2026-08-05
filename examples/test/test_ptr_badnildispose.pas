program TestPtrBadnildispose;
{ Disposing a nil pointer must be a clean Runtime Error, not a crash or
  silent corruption of the freelist. }
type
    PInt = ^integer;
var
    p: PInt;
begin
    p := nil;
    dispose(p);
end.
