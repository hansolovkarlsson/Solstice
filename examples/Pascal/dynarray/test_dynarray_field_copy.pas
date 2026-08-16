program TestDynArrayFieldCopy;
type
    TBox = record
        data: array of integer;
    end;
var
    b: TBox;
    c: array of integer;
    i: integer;
begin
    b.data := [1, 2, 3, 4];
    c := Copy(b.data, 1);
    writeln(Length(c));         { 3 }
    for i := 0 to High(c) do write(c[i], ' ');
    writeln;                     { 2 3 4 }
end.
