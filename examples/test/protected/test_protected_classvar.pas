program TestProtectedClassvar;
type
    TBase = class
    protected
        class var Count: integer;
    end;
    TSub = class(TBase)
        procedure Bump;
        function GetCount: integer;
    end;
var
    s: TSub;

procedure TSub.Bump;
begin
    Count := Count + 1;   { protected class var, accessed from a descendant - allowed }
end;

function TSub.GetCount;
begin
    GetCount := Count;
end;

begin
    new(s);
    s.Bump;
    s.Bump;
    writeln(s.GetCount);
    dispose(s);
end.
