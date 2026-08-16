program TestClassCtorNoargs;
type
    TCounter = class
        count: integer;
        procedure Init;
    end;
var
    c: TCounter;

procedure TCounter.Init;
begin
    count := 100;
end;

begin
    { zero-arg constructor call, no parens - matches the existing
      parenless-call convention for a method that takes no arguments. }
    new(c, Init);
    writeln('count: ', c.count);
    dispose(c);
end.
