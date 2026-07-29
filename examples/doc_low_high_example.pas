program DocLowHighExample;
var
    scores: array[-2..7] of integer;
    i: integer;
begin
    for i := low(scores) to high(scores) do
        scores[i] := i * i;
    writeln(length(scores));   { 10 }
end.
