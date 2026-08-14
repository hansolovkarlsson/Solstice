program TestArrayLiteralRecordField;
{ A GLOBAL record's fixed-array field also accepts array-literal
  assignment - falls out for free, since a record field is just another
  ordinary hidden global Symbol, going through the exact same
  parse_global_assignment() this whole feature is built on (that
  function is explicitly shared between plain global-variable and
  record-field assignment - see its own comment). A CLASS's own fixed-
  array field is a genuinely separate, heap-offset-based storage
  mechanism and still requires indexing - not extended by this feature. }
type
    TBox = record
        arr: array[1..3] of integer;
    end;
var
    b: TBox;
    i: integer;
begin
    b.arr := [7, 8, 9];
    for i := 1 to 3 do writeln(b.arr[i]);
end.
