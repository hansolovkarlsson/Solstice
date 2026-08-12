program TestProtBadPrivateStrict;
{ Confirms protected and private are genuinely different: a subclass can
  reach a protected member but still can't reach a private one, even in
  the same class. }
type
    TBase = class
    private
        secretPrivate: integer;
    protected
        secretProtected: integer;
    end;
    TSub = class(TBase)
        procedure TryAccess;
    end;
var
    s: TSub;

procedure TSub.TryAccess;
begin
    secretProtected := 1;   { fine - protected, we're a descendant }
    secretPrivate := 2;     { compile error - private, not even a descendant may touch it }
end;

begin
    new(s);
    s.TryAccess;
    dispose(s);
end.
