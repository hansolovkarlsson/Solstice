program TestProtectedBadUnrelated;
type
    TBase = class
    protected
        secret: integer;
    end;
    TOther = class
        procedure TryAccess(b: TBase);
    end;
var
    b: TBase;
    o: TOther;

procedure TOther.TryAccess;
begin
    b.secret := 5;
end;

begin
    new(b);
    new(o);
    o.TryAccess(b);
    dispose(b);
    dispose(o);
end.
