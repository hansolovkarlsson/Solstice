program TestWithBasic;
type
    TPoint = record
        x, y: integer;
        scores: array[1..3] of integer;
    end;
var
    p: TPoint;
    i: integer;
begin
    with p do begin
        x := 1;
        y := 2;
        for i := 1 to 3 do
            scores[i] := i * 10;
    end;
    writeln('p.x = ', p.x, ', p.y = ', p.y);
    writeln('p.scores[2] = ', p.scores[2]);

    with p do
        writeln('inside with: x=', x, ' y=', y, ' low(scores)=', low(scores), ' high(scores)=', high(scores));
end.
