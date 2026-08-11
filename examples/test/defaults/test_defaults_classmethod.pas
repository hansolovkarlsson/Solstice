program TestDefaultsClassmethod;
type
    TCounter = class
        value: integer;
        procedure Add(n: integer = 1);
    end;
var c: TCounter;

procedure TCounter.Add;
begin
    value := value + n;
end;

begin
    new(c);
    c.value := 0;
    c.Add;      { += 1 }
    c.Add(5);   { += 5 }
    writeln(c.value);  { 6 }
    dispose(c);
end.
