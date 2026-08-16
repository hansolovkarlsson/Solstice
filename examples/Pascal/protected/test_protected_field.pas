program TestProtectedField;
type
    TBase = class
    protected
        secret: integer;
    end;
    TSub = class(TBase)
        function DoubleSecret: integer;
    end;
var
    s: TSub;

function TSub.DoubleSecret;
begin
    secret := 21;   { protected field, accessed from a descendant class's own method - allowed }
    DoubleSecret := secret * 2;
end;

begin
    new(s);
    writeln(s.DoubleSecret);
    dispose(s);
end.
