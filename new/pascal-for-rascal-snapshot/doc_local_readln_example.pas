program DocLocalReadlnExample;

procedure greet;
var name: string;
begin
    readln(name);
    writeln('Hello, ', name);
end;

begin
    greet;
end.
