program TestClassCtorBasic;
type
    TCircle = class
        radius: real;
        procedure Init(r: real);
    end;
var
    c: TCircle;

procedure TCircle.Init;
begin
    radius := r;
end;

begin
    { allocate AND initialize in one statement }
    new(c, Init(2.0));
    writeln('radius: ', c.radius:0:5);
    dispose(c);
end.
