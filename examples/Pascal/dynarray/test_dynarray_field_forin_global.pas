program TestDynArrayFieldForInGlobal;
{ 'for x in someRecord.someField do' over a GLOBAL record variable's
  dynamic-array field - see docs/LANGUAGE.md#record-and-class-fields. }
type
    TBox = record
        data: array of integer;
    end;
var
    b: TBox;
    x, total: integer;
begin
    b.data := [1, 2, 3, 4];
    total := 0;
    for x in b.data do total := total + x;
    writeln(total);
end.
