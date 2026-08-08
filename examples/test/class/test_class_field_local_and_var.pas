program TestClassFieldLocalAndVar;
type
    TCircle = class
        radius: real;
    end;
var
    c: TCircle;

procedure Bump(var c: TCircle);
begin
    c.radius := c.radius + 1.0;
end;

procedure UseLocal;
var
    local: TCircle;
begin
    new(local);
    local.radius := 5.0;
    writeln('local radius: ', local.radius);
    dispose(local);
end;

begin
    new(c);
    c.radius := 1.0;
    Bump(c);
    writeln('after Bump: ', c.radius);
    dispose(c);
    UseLocal;
end.
