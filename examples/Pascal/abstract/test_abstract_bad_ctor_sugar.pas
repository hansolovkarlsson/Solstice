program TestAbstractBadCtorSugar;
type
    TShape = class
    public
        function Area: real; abstract;
        procedure Init;
    end;
var shape: TShape;

procedure TShape.Init;
begin
end;

begin
    new(shape, Init());
end.
