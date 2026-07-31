program TestArray2DBadOOB;
var
    board: array[1..3, 1..3] of integer;

procedure bad(b: array[1..3, 1..3] of integer);
begin
    b[5, 1] := 1;
end;

begin
    bad(board);
end.
