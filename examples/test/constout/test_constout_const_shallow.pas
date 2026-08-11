program TestConstoutConstShallow;
type
    TPoint = class
        x: integer;
    end;
var
    p: TPoint;

procedure TouchIt(const c: TPoint);
begin
    c.x := 77;
end;

begin
    new(p);
    p.x := 1;
    TouchIt(p);
    writeln(p.x);
    dispose(p);
end.
