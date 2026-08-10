program TestPropertyBadDuplicateField;
type
    TCircle = class
    private
        FRadius: real;
        Radius: real;
    public
        property Radius: real read FRadius;
    end;
var
    c: TCircle;
begin
    new(c);
    dispose(c);
end.
