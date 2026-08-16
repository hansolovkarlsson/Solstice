program TestProtectedBadToplevel;
type
    TBase = class
    protected
        secret: integer;
    end;
var
    b: TBase;
begin
    new(b);
    b.secret := 5;
    dispose(b);
end.
