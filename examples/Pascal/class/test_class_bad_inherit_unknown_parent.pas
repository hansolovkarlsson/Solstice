program TestClassBadInheritUnkParent;
type
    TCircle = class(TBogus)
        radius: real;
    end;
{ Expected: Compile Error - 'TBogus' is not a declared class }
var
    c: TCircle;
begin
end.
