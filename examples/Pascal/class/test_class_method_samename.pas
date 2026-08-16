program TestClassMethodSamename;
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
    { Two different classes, each with its own 'Area' method - proves
      mangled names (TCircle__Area / TSquare__Area) avoid the flat
      whole-program procedure namespace collision. }
    new(c);
    c.radius := 2.0;
    writeln('circle area: ', TCircle__Area(c));
    dispose(c);

    new(s);
    s.side := 4.0;
    writeln('square area: ', TSquare__Area(s));
    dispose(s);
end.
