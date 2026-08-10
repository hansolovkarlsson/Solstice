program PropBadWriteTargetUnknown;
type
    TCircle = class
    private
        FRadius: real;
    public
        property Radius: real read FRadius write NoSuchThing;
    end;
var
    c: TCircle;
begin
    new(c);
    dispose(c);
end.
