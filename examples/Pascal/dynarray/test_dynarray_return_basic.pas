program TestDynArrayReturnBasic;
var
    a: array of integer;

function MakeArr: array of integer;
begin
    SetLength(MakeArr, 2);
    MakeArr[0] := 7;
    MakeArr[1] := 8;
end;

begin
    a := MakeArr;
    writeln(a[0], ' ', a[1]);  { 7 8 }
end.
