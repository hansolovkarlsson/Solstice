program TestAliasDynArrayParams;
type
    TIntArray = array of integer;
var
    arr: TIntArray;

procedure Fill(var a: TIntArray; n: integer);
var i: integer;
begin
    SetLength(a, n);
    for i := 0 to n - 1 do a[i] := i;
end;

function Sum(a: TIntArray): integer;
var i, s: integer;
begin
    s := 0;
    for i := 0 to High(a) do s := s + a[i];
    Sum := s;
end;

begin
    Fill(arr, 4);
    writeln(Sum(arr));  { 0+1+2+3 = 6 }
end.
