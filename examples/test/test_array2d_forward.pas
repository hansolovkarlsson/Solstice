program TestArray2DForward;
var
    board: array[1..2, 1..2] of integer;

procedure fillIt(b: array[1..2, 1..2] of integer); forward;

procedure caller;
begin
    fillIt(board);
end;

procedure fillIt;
begin
    b[1, 1] := 7;
end;

begin
    caller;
    writeln('board[1,1] = ', board[1, 1]);
end.
