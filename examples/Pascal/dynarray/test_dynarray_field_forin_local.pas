program TestDynArrayFieldForInLocal;
{ 'for x in someRecord.someField do' over a LOCAL record variable's
  dynamic-array field (a procedure's own record local, not a
  parameter) - see docs/LANGUAGE.md#record-and-class-fields. }
type
    TBox = record
        data: array of integer;
    end;

procedure SumBox;
var
    b: TBox;
    x, total: integer;
begin
    b.data := [10, 20, 30];
    total := 0;
    for x in b.data do total := total + x;
    writeln(total);
end;

begin
    SumBox;
end.
