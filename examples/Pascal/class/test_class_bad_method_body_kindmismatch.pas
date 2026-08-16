program TestClassBadMethBodyKindMismatc;
type
    TCircle = class
        radius: real;
        function Area: real;
    end;
var
    c: TCircle;

procedure TCircle.Area;
begin
end;

{ Expected: Compile Error - 'TCircle.Area' was declared as a function,
  but its body is written as a procedure }
begin
end.
