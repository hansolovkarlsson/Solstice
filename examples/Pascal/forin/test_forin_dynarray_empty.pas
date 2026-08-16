program TestForinDynarrayEmpty;

{ SetLength(arr, 0), and a separate never-SetLength'd array - the loop
  body must run zero times in both cases, not error.
  count=0
  count=0 }

var
    arr, neverInit: array of integer;
    x, count: integer;

begin
    SetLength(arr, 0);
    count := 0;
    for x in arr do
        count := count + 1;
    writeln('count=', count);

    count := 0;
    for x in neverInit do
        count := count + 1;
    writeln('count=', count);
end.
