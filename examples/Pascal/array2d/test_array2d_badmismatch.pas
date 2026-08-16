program TestArray2DBadMismatch;
var
    board: array[1..3, 1..2] of integer;

procedure needs3x3(b: array[1..3, 1..3] of integer);
begin
end;

begin
    needs3x3(board);
end.
