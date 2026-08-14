program TestDynArrayFieldClass;
type
    TFoo = class
        data: array of integer;
    end;
var
    f: TFoo;
begin
    new(f);
    SetLength(f.data, 3);
    f.data[0] := 1;
    f.data[1] := 2;
    f.data[2] := 3;
    writeln(Length(f.data));                { 3 }
    writeln(f.data[0], ' ', f.data[1], ' ', f.data[2]);  { 1 2 3 }
    f.data := [10, 20, 30];                    { array-literal assignment }
    writeln(Length(f.data));                    { 3 }
    f.data[1] := 99;                             { indexed write }
    writeln(f.data[0], ' ', f.data[1], ' ', f.data[2]);  { 10 99 30 }
end.
