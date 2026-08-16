program TestRecordVariantBadLabelType;
type
    TShape = record
        case kind: integer of
            0: (radius: real);
            true: (width: real);
    end;
{ Expected: Compile Error - 'true' is a boolean label but the tag field
  'kind' is integer }
var
    s: TShape;
begin
end.
