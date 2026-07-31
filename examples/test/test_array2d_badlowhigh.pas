program TestArray2DBadLowHigh;
var
    board: array[1..3, 1..3] of integer;

procedure bad(b: array[1..3, 1..3] of integer);
begin
    writeln(low(b));
end;

begin
    bad(board);
end.
