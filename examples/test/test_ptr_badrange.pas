program TestPtrBadrange;
{ A subrange-typed field reached through a pointer must still be
  bounds-checked at runtime (NODE_RANGE_CHECK, wrapped at parse time
  exactly as it would be without a pointer involved) - a clean Runtime
  Error, not a silently out-of-range value. }
type
    TAge = 0..150;
    PEntry = ^TEntry;
    TEntry = record
        age: TAge;
    end;
var
    e: PEntry;
begin
    new(e);
    e^.age := 200;
end.
