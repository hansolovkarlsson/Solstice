program TestDArrFieldTCBadReassign;
{ A typed constant's dynamic-array field can't be reassigned (a new
  array-literal or otherwise), exactly like a scalar typed-constant
  field. }
type
    TBox = record
        data: array of integer;
    end;
const
    Bob: TBox = (data: [1, 2]);
begin
    Bob.data := [3, 4];
end.
