program TestClassInheritUpcast;
type
    TShape = class
        name: integer;
    end;
    TCircle = class(TShape)
        radius: real;
    end;
var
    c: TCircle;
    s: TShape;

begin
    new(c);
    c.name := 5;
    { assigning a subclass instance to an ancestor-typed variable }
    s := c;
    writeln('s.name via upcast assignment: ', s.name);
    if s = c then
        writeln('s and c compare equal (same instance)')
    else
        writeln('unexpected: not equal');
    dispose(c);
end.
