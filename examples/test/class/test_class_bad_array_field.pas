program TestClassBadArrayField;
type
    TBuffer = class
        data: array[1..10] of integer;
    end;
{ Expected: Compile Error - array-typed fields aren't supported in a
  class yet (see docs/ROADMAP.md). Nested-record fields are supported
  now - see test_class_composite_basic.pas - only arrays remain a gap. }
var
    b: TBuffer;
begin
end.
