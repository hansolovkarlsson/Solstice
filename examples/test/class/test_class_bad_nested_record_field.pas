program TestClassBadNestedRecordField;
type
    TPoint = record
        x, y: integer;
    end;
    TShape = class
        origin: TPoint;
    end;
{ Expected: Compile Error - only scalar field types are supported in a
  class yet; a nested-record field (composition) isn't supported (see
  notes/classes-and-instances-scoping.md) }
var
    s: TShape;
begin
end.
