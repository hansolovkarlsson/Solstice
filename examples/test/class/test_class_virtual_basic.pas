program TestClassVirtualBasic;
type
    TShape = class
        function Area: real;
    end;
    TCircle = class(TShape)
        radius: real;
        function Area: real;
    end;
    TSquare = class(TShape)
        side: real;
        function Area: real;
    end;
var
    s: TShape;
    c: TCircle;
    sq: TSquare;

function TShape.Area;
begin
    Area := 0.0;
end;

function TCircle.Area;
begin
    Area := 3.14159 * self.radius * self.radius;
end;

function TSquare.Area;
begin
    Area := self.side * self.side;
end;

begin
    new(c);
    c.radius := 2.0;
    new(sq);
    sq.side := 4.0;

    { s is statically typed TShape, but holds a TCircle/TSquare instance -
      dynamic dispatch must call the SUBCLASS's own overridden Area, not
      TShape's own (which would always print 0.0 under static dispatch). }
    s := c;
    writeln('circle area via TShape ref: ', s.Area:0:5);
    s := sq;
    writeln('square area via TShape ref: ', s.Area:0:5);

    { direct calls (no upcast) must still dispatch correctly too. }
    writeln('circle area direct: ', c.Area:0:5);
    writeln('square area direct: ', sq.Area:0:5);

    dispose(c);
    dispose(sq);
end.
