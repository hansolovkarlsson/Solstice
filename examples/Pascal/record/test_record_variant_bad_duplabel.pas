program TestRecordVariantBadDupLabel;
type
    TShape = record
        case kind: integer of
            0: (radius: real);
            0: (width: real);
    end;
{ Expected: Compile Error - variant label 0 used twice }
var
    s: TShape;
begin
end.
