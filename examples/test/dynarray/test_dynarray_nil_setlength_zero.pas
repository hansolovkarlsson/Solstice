program TestDynarrayNilSetlengthZero;

{ SetLength(arr, 0) after the array held real data, then arr = nil is
  true - the specific case motivating the whole codegen design: a
  zero-length array (value 0) must compare equal to nil, not just to
  itself. yes }

var
    arr: array of integer;

begin
    SetLength(arr, 3);
    arr[0] := 1;
    arr[1] := 2;
    arr[2] := 3;
    SetLength(arr, 0);
    if arr = nil then writeln('yes') else writeln('no');
end.
