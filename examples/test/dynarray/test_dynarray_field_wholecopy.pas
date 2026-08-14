program TestDynArrayFieldWholeCopy;
type
    TBox = record
        tag: integer;
        data: array of integer;
    end;
var
    a, b: TBox;
begin
    a.tag := 1;
    SetLength(a.data, 2);
    a.data[0] := 10;
    a.data[1] := 20;
    b := a;                { whole-record copy - shallow, like a pointer field }
    b.data[0] := 999;
    writeln(a.data[0]);    { 999 - shared storage, same as any other dynarray reference }
    writeln(b.tag);         { 1 - scalar field copied independently }
end.
