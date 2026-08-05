program TestRecarrTypes;
{ A record-array field keeps its own declared type's normal validation -
  a subrange field's bounds are still enforced (see wrap_range_check() in
  parser.c), and a char field still requires exactly one character - both
  exactly as if it were a plain (non-array) record's field. Expected
  output:
  A: 30
  B: 65 }
type
    TAge = 0..150;
    TEntry = record
        initial: char;
        age: TAge;
    end;
var
    entries: array[1..2] of TEntry;
    i: integer;
begin
    entries[1].initial := 'A';
    entries[1].age := 30;
    entries[2].initial := 'B';
    entries[2].age := 65;
    for i := 1 to 2 do
        writeln(entries[i].initial, ': ', entries[i].age);
end.
