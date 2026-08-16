program TestDynArrayReturnLiteral;
var
    a: array of integer;

function MakeArr: array of integer;
begin
    MakeArr := [1, 2, 3];  { array-literal assignment to the function's own name }
end;

begin
    a := MakeArr;
    writeln(a[0], ' ', a[1], ' ', a[2]);  { 1 2 3 }
end.
