program TestLowHigh;
var
    scores: array[-2..7] of integer;
    i, total: integer;
begin
    writeln('low(scores) = ', low(scores));     { -2 }
    writeln('high(scores) = ', high(scores));   { 7 }
    writeln('length(scores) = ', length(scores)); { 10 }

    { the classic idiom - low/high directly as for-loop bounds }
    for i := low(scores) to high(scores) do
        scores[i] := i * i;

    total := 0;
    for i := low(scores) to high(scores) do
        total := total + scores[i];
    writeln('total = ', total);

    { confirm length still works on strings, unaffected }
    writeln('length(hello) = ', length('hello'));
end.
