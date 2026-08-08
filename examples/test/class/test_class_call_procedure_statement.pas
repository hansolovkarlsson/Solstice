program TestClassCallProcStmt;
type
    TCounter = class
        count: integer;
        procedure AddTo(var total: integer);
        procedure Bump;
    end;
var
    c: TCounter;
    total: integer;

procedure TCounter.AddTo;
begin
    total := total + self.count;
end;

procedure TCounter.Bump;
begin
    self.count := self.count + 1;
end;

begin
    new(c);
    c.count := 5;
    total := 10;
    c.AddTo(total);
    writeln('total: ', total);

    { zero-arg procedure method call as a bare statement, no parens }
    c.Bump;
    writeln('count: ', c.count);
    dispose(c);
end.
