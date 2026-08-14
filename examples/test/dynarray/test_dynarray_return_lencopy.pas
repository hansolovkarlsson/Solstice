program TestDynArrayReturnLenCopy;
var
    a: array of integer;

function MakeArr: array of integer;
begin
    SetLength(MakeArr, 3);
    MakeArr[0] := 10; MakeArr[1] := 20; MakeArr[2] := 30;
    writeln(Length(MakeArr));  { 3, read mid-body }
    writeln(High(MakeArr));    { 2, read mid-body }
end;

begin
    a := Copy(MakeArr, 1);  { Copy reads MakeArr's own name at the call site }
    writeln(a[0], ' ', a[1]);  { 20 30 }
end.
