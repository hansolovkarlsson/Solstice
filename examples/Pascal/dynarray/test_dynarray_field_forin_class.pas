program TestDynArrayFieldForInClass;
{ 'for x in someInstance.someField do' over a CLASS instance's
  dynamic-array field - heap-based storage, a genuinely separate code
  path from the plain-record-field case (see
  docs/LANGUAGE.md#record-and-class-fields). }
type
    TFoo = class
        data: array of integer;
    end;
var
    f: TFoo;
    x, total: integer;
begin
    new(f);
    f.data := [10, 20, 30, 40];
    total := 0;
    for x in f.data do total := total + x;
    writeln(total);                 { 100 }
end.
