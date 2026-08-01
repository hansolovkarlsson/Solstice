program TestLocalForRecursion;

procedure factorialViaLoop(n: integer);
var i, result: integer;
begin
    result := 1;
    for i := 1 to n do
        result := result * i;
    { recurse AFTER the loop has run, with a DIFFERENT bound each time -
      if the hidden end-bound cache were shared (global) instead of
      per-call isolated, this recursive call corrupting it would only
      matter if it ran DURING the loop, but this specifically tests that
      each call's own loop and bound are fully independent }
    if n < 5 then
        writeln('factorialViaLoop(', n, ') = ', result, ', recursing to n+1...')
    else
        writeln('factorialViaLoop(', n, ') = ', result);
    if n < 5 then
        factorialViaLoop(n + 1);
end;

function sumNested(outer, inner: integer): integer;
var i, j, total: integer;
begin
    total := 0;
    for i := 1 to outer do
        for j := 1 to inner do
            total := total + 1;
    sumNested := total;
end;

begin
    factorialViaLoop(1);
    writeln('sumNested(3,4) = ', sumNested(3, 4));  { 12 }
end.
