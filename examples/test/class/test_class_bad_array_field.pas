program TestClassBadArrayField;
type
    TBuffer = class
        data: array[1..10] of integer;
    end;
{ Expected: Compile Error - array fields aren't supported in a class yet
  (scalar-only v1 gap, see notes/classes-and-instances-scoping.md) }
var
    b: TBuffer;
begin
end.
