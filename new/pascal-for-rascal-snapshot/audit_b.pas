program AuditB;
type TPerson = record name: string; end;
var p: TPerson;
begin
    p.name := 'hello world';
    writeln(length(p.name));
    writeln(copy(p.name, 1, 5));
    writeln(uppercase(p.name));
    writeln(p.name[1]);
end.
