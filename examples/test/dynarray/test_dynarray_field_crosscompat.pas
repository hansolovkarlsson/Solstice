program TestDynArrayFieldCrossCompat;
type
    TBox = record
        data: array of integer;
    end;
var
    b: TBox;
    direct: array of integer;
begin
    SetLength(direct, 2);
    direct[0] := 7;
    direct[1] := 8;
    b.data := direct;   { same structurally-deduped shape as a plain 'array of integer' }
    writeln(b.data[0], ' ', b.data[1]);  { 7 8 }
end.
