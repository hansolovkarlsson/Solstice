program TestConstoutWarnOutUnassigned;
var
    a: integer;

procedure Forgetful(out y: integer);
begin
    writeln('did nothing');
end;

begin
    Forgetful(a);
end.
