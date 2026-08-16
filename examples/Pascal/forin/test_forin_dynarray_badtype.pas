program TestForinDynarrayBadtype;

{ The loop variable's declared type doesn't match the dynamic array's
  element type - confirms the existing type-mismatch error still fires
  for the dynamic path, not just the static one. }

var
    arr: array of integer;
    x: boolean;

begin
    SetLength(arr, 1);
    arr[0] := 1;
    for x in arr do
        writeln(x);
end.
