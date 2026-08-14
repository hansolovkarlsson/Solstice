program TestForinDynarraySetlenMidloop;

{ The loop body calls SetLength on the SAME array being iterated -
  confirms the upper bound (Length(arr) - 1) is cached ONCE before the
  loop starts, not re-read on every iteration (which would either loop
  forever/too long as the array grows, or skip elements as it shrinks).
  Exactly 3 iterations, matching the array's length AT THE START of the
  loop, regardless of the SetLength calls inside it.
  1 2 3
  count=3 }

var
    arr: array of integer;
    x, count: integer;

begin
    SetLength(arr, 3);
    arr[0] := 1;
    arr[1] := 2;
    arr[2] := 3;
    count := 0;
    for x in arr do begin
        write(x, ' ');
        count := count + 1;
        SetLength(arr, 10); { grows the array mid-loop - must NOT affect the bound already cached }
    end;
    writeln;
    writeln('count=', count);
end.
