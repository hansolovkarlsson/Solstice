program TestForinDynarrayLocal;

{ Same, for a LOCAL dynamic array inside a procedure - both the array
  AND the loop variable are local, exercising parse_for_in_tail_local()'s
  new branch. 60 }

procedure Sum;
    var
        arr: array of integer;
        x, total: integer;
begin
    SetLength(arr, 3);
    arr[0] := 10;
    arr[1] := 20;
    arr[2] := 30;
    total := 0;
    for x in arr do
        total := total + x;
    writeln(total);
end;

begin
    Sum;
end.
