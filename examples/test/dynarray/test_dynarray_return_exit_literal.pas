program TestDynArrayReturnExitLiteral;
var
    a: array of integer;

function MakeArr: array of integer;
begin
    exit([9, 8, 7]);  { exit(value) with an array literal }
end;

begin
    a := MakeArr;
    writeln(a[0], ' ', a[1], ' ', a[2]);  { 9 8 7 }
end.
