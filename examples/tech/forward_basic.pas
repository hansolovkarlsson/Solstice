program ForwardBasic;
var
    result: boolean;

procedure isOdd(n: integer); forward;

procedure isEven(n: integer);
begin
    if n = 0 then
        result := true
    else
        isOdd(n - 1);
end;

procedure isOdd;
begin
    if n = 0 then
        result := false
    else
        isEven(n - 1);
end;

begin
    isEven(10);
    writeln('10 is even: ', result);
    isOdd(10);
    writeln('10 is odd: ', result);
end.
