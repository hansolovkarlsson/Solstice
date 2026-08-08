program TestClassCallSamename;
type
    TCircle = class
        radius: real;
        function Area: real;
    end;
    TSquare = class
        side: real;
        function Area: real;
    end;
var
    c: TCircle;
    s: TSquare;

function TCircle.Area;
begin
    Area := 3.14159 * self.radius * self.radius;
end;

function TSquare.Area;
begin
    Area := self.side * self.side;
end;

begin
    { c.Area and s.Area each dispatch to the right class's own mangled
      method, even though the short 'Area' name is shared. }
    new(c);
    c.radius := 2.0;
    writeln('circle area: ', c.Area);
    dispose(c);

    new(s);
    s.side := 4.0;
    writeln('square area: ', s.Area);
    dispose(s);
end.
