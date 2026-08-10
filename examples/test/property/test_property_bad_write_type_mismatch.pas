program PropBadWriteTypeMismatch;
type
    TCircle = class
    private
        FRadius: real;
        FCount: integer;
    public
        property Radius: real read FRadius write FCount;
    end;
var
    c: TCircle;
begin
    new(c);
    dispose(c);
end.
