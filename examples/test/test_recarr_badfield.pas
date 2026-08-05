program TestRecarrBadfield;
{ Referencing a field name that doesn't exist on the record type an array
  is declared over must be a clear Compile Error. }
type
    TPoint = record
        x, y: integer;
    end;
var
    pts: array[1..2] of TPoint;
begin
    pts[1].z := 1;
end.
