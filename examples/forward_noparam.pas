program ForwardNoParam;
var counter: integer;

procedure pingPong; forward;

procedure ping;
begin
    counter := counter + 1;
    writeln('ping ', counter);
    if counter < 6 then
        pingPong;
end;

procedure pingPong;
begin
    counter := counter + 1;
    writeln('pong ', counter);
    if counter < 6 then
        ping;
end;

begin
    counter := 0;
    ping;
end.
