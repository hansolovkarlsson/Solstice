program TestPropertyBadPrivateAccess;
type
    TCircle = class
    private
        FRadius: real;
        property Radius: real read FRadius;
    end;
var
    c: TCircle;
begin
    new(c);
    writeln(c.Radius:0:2);
    dispose(c);
end.
