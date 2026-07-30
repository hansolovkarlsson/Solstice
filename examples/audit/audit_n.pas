program AuditN;
type TPerson = record age: integer; end;
var p: TPerson;
begin
    readln(p.age);
    writeln('age: ', p.age);
end.
