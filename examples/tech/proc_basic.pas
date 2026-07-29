program ProcBasic;
var
    x: integer;

procedure greet;
begin
    writeln('Hello from greet, x=', x);
end;

begin
    x := 42;
    greet;
    x := 100;
    greet();
end.
