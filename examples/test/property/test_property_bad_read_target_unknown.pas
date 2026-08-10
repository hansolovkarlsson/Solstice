program PropBadReadTargetUnknown;
type
    TCircle = class
    private
        FRadius: real;
    public
        property Radius: real read NoSuchThing;
    end;
var
    c: TCircle;
begin
    new(c);
    dispose(c);
end.
