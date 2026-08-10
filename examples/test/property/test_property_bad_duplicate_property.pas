program PropBadDupProperty;
type
    TCircle = class
    private
        FRadius: real;
        FDiameter: real;
    public
        property Radius: real read FRadius;
        property Radius: real read FDiameter;
    end;
var
    c: TCircle;
begin
    new(c);
    dispose(c);
end.
