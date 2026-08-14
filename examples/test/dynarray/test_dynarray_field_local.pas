program TestDynArrayFieldLocal;
type
    TBox = record
        data: array of integer;
    end;

procedure DoIt;
var
    b: TBox;
    i: integer;
begin
    SetLength(b.data, 3);
    b.data[0] := 5;
    b.data[1] := 6;
    b.data[2] := 7;
    writeln(Length(b.data));                { 3 }
    for i := 0 to High(b.data) do write(b.data[i], ' ');
    writeln;                                   { 5 6 7 }
    b.data := [100, 200];
    writeln(b.data[0], ' ', b.data[1]);          { 100 200 }
end;

begin
    DoIt;
end.
