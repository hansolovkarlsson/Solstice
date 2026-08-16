program TestProtectedInheritedInit;
type
    TBase = class
    protected
        v: integer;
        procedure Init;
    end;
    TSub = class(TBase)
        procedure Init;
        function GetV: integer;
    end;
var
    s: TSub;

procedure TBase.Init;
begin
    v := 1;
end;

procedure TSub.Init;
begin
    inherited Init;   { protected constructor-style method, called via 'inherited' from a descendant - allowed }
    v := v + 1;
end;

function TSub.GetV;
begin
    GetV := v;
end;

begin
    new(s, Init);
    writeln(s.GetV);
    dispose(s);
end.
