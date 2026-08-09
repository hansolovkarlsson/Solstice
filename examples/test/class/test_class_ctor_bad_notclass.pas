program TestClassCtorBadNotclass;
type
    PInt = ^integer;
var
    p: PInt;
begin
    { p is a plain (non-class) pointer - 'new(x, Method(...))' is only
      valid for a class-typed target. Expected: Compile Error. }
    new(p, Foo());
end.
