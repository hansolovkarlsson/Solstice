program TestDynarrayNilAssign;

{ arr := nil; then arr = nil is true, and a subsequent SetLength/
  element write works correctly afterward - confirms nil's -1 doesn't
  confuse SetLength's own "is there an old block" check.
  is_nil=true
  10 20 }

var
    arr: array of integer;

begin
    arr := nil;
    if arr = nil then writeln('is_nil=true') else writeln('is_nil=false');
    SetLength(arr, 2);
    arr[0] := 10;
    arr[1] := 20;
    writeln(arr[0], ' ', arr[1]);
end.
