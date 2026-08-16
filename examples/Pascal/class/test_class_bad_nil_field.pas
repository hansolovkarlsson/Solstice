program TestClassBadNilField;
type
    TCircle = class
        radius: real;
    end;
var
    c: TCircle;
begin
    { Explicitly nil, then dereferenced. Expected: VM Runtime Error -
      nil pointer dereference (the existing pointer nil-check, reused
      for free since a class variable is an ordinary pointer variable). }
    c := nil;
    c.radius := 1.0;
end.
