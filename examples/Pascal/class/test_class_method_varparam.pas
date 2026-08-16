program TestClassMethodVarparam;
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
var
    step: integer;
begin
    step := 1;
    self.count := self.count + step;
end;

begin
    new(c);
    c.count := 5;
    total := 10;
    TCounter__AddTo(c, total);
    writeln('total: ', total);

    TCounter__Bump(c);
    writeln('count after bump: ', c.count);
    dispose(c);
end.
