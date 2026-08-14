program TestDArrFieldTCBadSetLen;
{ A typed constant's dynamic-array field is immutable, exactly like a
  scalar typed-constant field - SetLength on it is rejected. }
type
    TBox = record
        data: array of integer;
    end;
const
    Bob: TBox = (data: [1, 2]);
begin
    SetLength(Bob.data, 5);
end.
