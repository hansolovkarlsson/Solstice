program ParamRecursive;
var
    result: integer;

procedure sumTo(n: integer);
var
    partial: integer;
begin
    if n = 0 then
        partial := 0
    else begin
        sumTo(n - 1);
        partial := n + result;
    end;
    result := partial;
end;

begin
    sumTo(5);
    writeln('sum 1..5 = ', result);
end.
