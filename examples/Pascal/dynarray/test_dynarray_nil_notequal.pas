program TestDynarrayNilNotequal;

{ SetLength(arr, 3) with data - arr <> nil is true, arr = nil is false.
  notnil=true
  isnil=false }

var
    arr: array of integer;

begin
    SetLength(arr, 3);
    arr[0] := 1;
    if arr <> nil then writeln('notnil=true') else writeln('notnil=false');
    if arr = nil then writeln('isnil=true') else writeln('isnil=false');
end.
