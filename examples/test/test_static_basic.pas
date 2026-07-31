program TestStaticBasic;
var
    i: integer;

function counter: integer;
var
    static n: integer;
begin
    inc(n);
    counter := n;
end;

begin
    for i := 1 to 3 do
        writeln('counter = ', counter);
end.
