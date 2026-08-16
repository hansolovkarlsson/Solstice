program TestAbstractBadInstSub;
type
    TShape = class
    public
        function Area: real; abstract;
    end;
    TCircle = class(TShape)
    public
        procedure SetRadius(r: real);
    end;
var c: TCircle;

procedure TCircle.SetRadius;
begin
end;

begin
    new(c);
end.
