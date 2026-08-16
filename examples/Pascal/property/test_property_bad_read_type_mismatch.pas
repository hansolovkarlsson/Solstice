program TestPropertyBadReadTypeMismatch;
type
    TCircle = class
    private
        FCount: integer;
    public
        property Radius: real read FCount;
    end;
var
    c: TCircle;
begin
    new(c);
    dispose(c);
end.
