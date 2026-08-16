program TestInheritedExplicit;

// 'inherited MethodName(args);' - an override calling its ancestor's own
// implementation with explicit arguments, then building on top of it.
// TShape.Describe = name*10. TCircle.Describe = inherited Describe() +
// radius, i.e. (3*10) + 100 = 130. TCircle.SetName forwards its own
// argument on to TShape.SetName via an explicit (not bare) inherited
// call, so name ends up 3.

type
  TShape = class
    name: integer;
    function Describe: integer;
    procedure SetName(n: integer);
  end;

  TCircle = class(TShape)
    radius: integer;
    function Describe: integer;
    procedure SetName(n: integer);
  end;

var c: TCircle;

function TShape.Describe;
begin
  Describe := name * 10;
end;

procedure TShape.SetName;
begin
  name := n;
end;

function TCircle.Describe;
begin
  Describe := inherited Describe() + radius;
end;

procedure TCircle.SetName;
begin
  inherited SetName(n);
  writeln(name);
end;

begin
  new(c);
  c.SetName(3);
  c.radius := 100;
  writeln(c.Describe);
  dispose(c);
end.
