program AuditA;
type TPerson = record name: string; end;
var p: TPerson;
begin
    p.name := 'Hello';
    p.name[1] := 'J';
    writeln(p.name);
end.
