program TestRecnestBadarrayfield;
{ Expected: Compile Error - TPoly (has array field 'pts') can't be used as a nested field }
type
    TPoly = record
        pts: array[1..3] of integer;
    end;
    TShape = record
        poly: TPoly;
    end;
var
    s: TShape;
begin
end.
