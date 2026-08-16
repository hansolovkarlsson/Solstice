program TestAbstractBadInstBase;
type
    TShape = class
    public
        function Area: real; abstract;
    end;
var shape: TShape;
begin
    new(shape);
end.
