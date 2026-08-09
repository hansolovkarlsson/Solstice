program TestClassCompositeBasic;
type
    TPoint = record
        x, y: integer;
    end;
    TCircle = class
        center: TPoint;
        radius: real;
        procedure SetCenter(cx, cy: integer);
        function DistFromOrigin: integer;
    end;
var
    c1, c2: TCircle;

procedure TCircle.SetCenter;
begin
    { explicit self.center.field }
    self.center.x := cx;
    self.center.y := cy;
end;

function TCircle.DistFromOrigin;
begin
    { bare shorthand center.field, mixed with a plain scalar field }
    DistFromOrigin := center.x + center.y;
end;

begin
    new(c1);
    c1.SetCenter(3, 4);
    c1.radius := 1.0;

    new(c2);
    c2.SetCenter(100, 200);
    c2.radius := 2.0;

    { explicit c.center.field read }
    writeln('c1 center: ', c1.center.x, ', ', c1.center.y);
    writeln('c2 center: ', c2.center.x, ', ', c2.center.y);
    writeln('c1 dist: ', c1.DistFromOrigin);
    writeln('c2 dist: ', c2.DistFromOrigin);

    { mutating c2 must not affect c1 - proves per-instance heap sizing/
      offset correctness, not just correct offsets within one instance }
    c2.center.x := 999;
    writeln('c1 center unchanged: ', c1.center.x, ', ', c1.center.y);
    writeln('c2 center changed: ', c2.center.x, ', ', c2.center.y);

    dispose(c1);
    dispose(c2);
end.
