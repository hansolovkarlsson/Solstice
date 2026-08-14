program TestForinDynarrayVarparam;

{ A dynamic array received as a 'var' parameter, iterated inside the
  callee - dynamic array parameters are already "just an ordinary (or
  var) scalar parameter", so this should need no special-casing. 60 }

var
    arr: array of integer;
    result: integer;

procedure Sum(var arr: array of integer; var total: integer);
    var x: integer;
begin
    total := 0;
    for x in arr do
        total := total + x;
end;

begin
    SetLength(arr, 3);
    arr[0] := 10;
    arr[1] := 20;
    arr[2] := 30;
    Sum(arr, result);
    writeln(result);
end.
