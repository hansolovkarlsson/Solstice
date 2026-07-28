program ParamBasic;

procedure printDouble(x: integer);
var
    temp: integer;
begin
    temp := x * 2;
    writeln('double of ', x, ' is ', temp);
end;

procedure greet(name: string; times: integer);
var
    i: integer;
begin
    i := 1;
    while i <= times do begin
        writeln('Hello, ', name, '!');
        i := i + 1;
    end;
end;

begin
    printDouble(21);
    printDouble(100);
    greet('World', 3);
end.
