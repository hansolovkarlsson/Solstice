program TestForinDynarraySubrangeElem;

{ A 'byte' element type - confirms the per-iteration assignment is still
  range-checked via wrap_range_check(), matching the static-array case.
  10 200 30 }

var
    arr: array of byte;
    x: integer;

begin
    SetLength(arr, 3);
    arr[0] := 10;
    arr[1] := 200;
    arr[2] := 30;
    for x in arr do
        write(x, ' ');
    writeln;
end.
