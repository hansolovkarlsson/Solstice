program TestForInEvalOnce;
var
    calls: integer;
    x: integer;

function BuildSet: integer;
begin
    calls := calls + 1;
    BuildSet := 3;
end;

begin
    calls := 0;
    { the set expression must be evaluated exactly once, before the loop
      starts - not once per candidate bit (0..31) }
    for x in [1, 2, BuildSet] do
        writeln(x);
    writeln('calls=', calls);
end.
