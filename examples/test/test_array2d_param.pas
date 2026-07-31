program TestArray2DParam;
var
    board: array[1..3, 1..3] of integer;

procedure fillBoard(b: array[1..3, 1..3] of integer);
var
    i, j: integer;
begin
    for i := 1 to 3 do
        for j := 1 to 3 do
            b[i, j] := i * 10 + j;
end;

procedure printBoard(b: array[1..3, 1..3] of integer);
var
    i, j: integer;
begin
    for i := 1 to 3 do begin
        for j := 1 to 3 do
            write(b[i, j], ' ');
        writeln;
    end;
end;

begin
    fillBoard(board);
    printBoard(board);
end.
