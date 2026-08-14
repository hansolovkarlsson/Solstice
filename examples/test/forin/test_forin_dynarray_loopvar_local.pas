program TestForinDynarrayLoopvarLocal;

{ The LOOP VARIABLE x is local while the array itself is a GLOBAL
  dynamic array - exercises parse_for_in_tail_local()'s new branch
  specifically, with the array resolved as a global inside it. 60 }

var
    arr: array of integer;

procedure Sum;
    var
        x, total: integer;
begin
    total := 0;
    for x in arr do
        total := total + x;
    writeln(total);
end;

begin
    SetLength(arr, 3);
    arr[0] := 10;
    arr[1] := 20;
    arr[2] := 30;
    Sum;
end.
