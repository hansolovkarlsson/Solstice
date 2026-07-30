program DocArraysExample;
var
    scores: array[1..5] of integer;
    i, total: integer;

function sumOf(arr: array[1..5] of integer): integer;
var
    k, s: integer;
begin
    s := 0;
    k := 1;
    while k <= 5 do begin
        s := s + arr[k];
        k := k + 1;
    end;
    sumOf := s;
end;

begin
    for i := 1 to 5 do
        scores[i] := i * 10;
    total := sumOf(scores);
    writeln('total = ', total);   { 150 }
end.
