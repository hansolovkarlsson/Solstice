program TestRecordArrayFieldWhole;
type TStudent = record id: integer; scores: array[1..3] of integer; end;
var a, b: TStudent;
begin
    a.id := 1;
    b := a;
end.
