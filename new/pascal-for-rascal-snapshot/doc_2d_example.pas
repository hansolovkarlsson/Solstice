program Doc2DExample;
var
    board: array[1..3, 1..3] of char;
    row, col: integer;
begin
    for row := 1 to 3 do
        for col := 1 to 3 do
            board[row, col] := '.';
    board[2, 2] := 'X';

    for row := 1 to 3 do begin
        for col := 1 to 3 do
            write(board[row, col]);
        writeln;
    end;
end.
