program TestInheritedBare;

// Bare 'inherited;' - same method name, arguments forwarded unchanged
// from the currently executing method's own parameters. TCircle.SetName
// forwards its 'n' parameter straight to TShape.SetName without naming
// it or its arguments explicitly. name ends up 3.

type
  TShape = class
    name: integer;
    procedure SetName(n: integer);
  end;

  TCircle = class(TShape)
    procedure SetName(n: integer);
  end;

var c: TCircle;

procedure TShape.SetName;
begin
  name := n;
end;

procedure TCircle.SetName;
begin
  inherited;
  writeln(name);
end;

begin
  new(c);
  c.SetName(3);
  dispose(c);
end.
