program TestRecarrBadrange;
{ A subrange-typed field of a record-array element must still be bounds-
  checked at runtime (via NODE_RANGE_CHECK, wrapped at parse time exactly
  as it would be for a plain record's own field) - a clean Runtime Error,
  not a silently out-of-range value. }
type
    TAge = 0..150;
    TEntry = record
        age: TAge;
    end;
var
    entries: array[1..2] of TEntry;
begin
    entries[1].age := 200;
end.
