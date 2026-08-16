program TestProtectedGrandchild;
type
    TBase = class
    protected
        secret: integer;
    end;
    TMiddle = class(TBase)
    end;
    TLeaf = class(TMiddle)
        function DoubleSecret: integer;
    end;
var
    l: TLeaf;

function TLeaf.DoubleSecret;
begin
    secret := 7;   { protected field, accessed two inheritance levels down - allowed }
    DoubleSecret := secret * 2;
end;

begin
    new(l);
    writeln(l.DoubleSecret);
    dispose(l);
end.
