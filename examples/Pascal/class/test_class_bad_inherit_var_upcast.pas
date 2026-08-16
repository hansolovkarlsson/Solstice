program TestClassBadInheritVarUpcast;
type
    TShape = class
        name: integer;
    end;
    TCircle = class(TShape)
        radius: real;
    end;
var
    c: TCircle;

procedure TakesShapeVar(var s: TShape);
begin
end;

begin
    { 'var' parameters never widen, including for class upcasts - real
      Pascal never widens a 'var' argument, and that rule applies here
      unchanged. Expected: Compile Error - wrong type. }
    new(c);
    TakesShapeVar(c);
    dispose(c);
end.
