program TestDynArrayFieldBadForIn;
{ Known, documented limitation: 'for x in ...' doesn't yet recognize a
  record/class field as a valid iterable, even though the exact same
  expression works everywhere else (indexing, Length, Copy, SetLength).
  See docs/LANGUAGE.md#dynamic-arrays' "What's not supported yet". }
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
