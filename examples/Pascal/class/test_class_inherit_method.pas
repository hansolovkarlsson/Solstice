program TestClassInheritMethod;
type
    TShape = class
        name: integer;
        function Describe: integer;
    end;
    TCircle = class(TShape)
        radius: real;
    end;
var
    c: TCircle;

function TShape.Describe;
begin
    Describe := self.name;
end;

procedure TakesShape(s: TShape);
begin
    writeln('via by-value param (upcast): ', s.Describe);
end;

begin
    new(c);
    c.name := 42;
    { TCircle never overrides Describe - calling it through the
      subclass instance dispatches to TShape's own implementation, and
      self (typed TShape) accepts the TCircle instance via upcast. }
    writeln('direct: ', c.Describe);
    { passing a TCircle where an ordinary procedure expects a TShape -
      the same upcast compatibility that makes 'self' work above. }
    TakesShape(c);
    dispose(c);
end.
