program TestDynArrayFieldGlobal;
type
    TBox = record
        data: array of integer;
    end;
var
    b: TBox;
begin
    SetLength(b.data, 3);
    b.data[0] := 10;
    b.data[1] := 20;
    b.data[2] := 30;
    writeln(Length(b.data));                      { 3 }
    writeln(b.data[0], ' ', b.data[1], ' ', b.data[2]);  { 10 20 30 }
    b.data := [1, 2, 3, 4];                        { array-literal assignment }
    writeln(Length(b.data));                        { 4 }
    writeln(b.data[0], ' ', b.data[3]);              { 1 4 }
end.
