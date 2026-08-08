program TestRecordVariantBadDupField;
type
    TShape = record
        radius: real;
        case kind: integer of
            0: (radius: real);
    end;
{ Expected: Compile Error - variant field 'radius' collides with the
  fixed field of the same name - field names must be unique across the
  whole record, including across variants }
var
    s: TShape;
begin
end.
