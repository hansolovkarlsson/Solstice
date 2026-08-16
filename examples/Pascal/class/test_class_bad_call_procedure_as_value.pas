program TestClassBadCallProcAsValue;
type
    TCounter = class
        count: integer;
        procedure Bump;
    end;
var
    c: TCounter;

procedure TCounter.Bump;
begin
    self.count := self.count + 1;
end;

begin
    new(c);
    writeln('x: ', c.Bump);
    dispose(c);
end.
