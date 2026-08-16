program TestDynArrayReturnMethod;
type
    TFoo = class
        function MakeArr: array of integer;
    end;
var
    f: TFoo;
    a: array of integer;

function TFoo.MakeArr;
begin
    SetLength(MakeArr, 2);
    MakeArr[0] := 42;
    MakeArr[1] := 43;
end;

begin
    new(f);
    a := f.MakeArr;
    writeln(a[0], ' ', a[1]);  { 42 43 }
end.
