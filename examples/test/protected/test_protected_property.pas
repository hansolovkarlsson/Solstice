program TestProtectedProperty;
type
    TBase = class
    protected
        fVal: integer;
        function GetVal: integer;
        procedure SetVal(v: integer);
        property Val: integer read GetVal write SetVal;
    end;
    TSub = class(TBase)
        function Doubled: integer;
    end;
var
    s: TSub;

function TBase.GetVal;
begin
    GetVal := fVal;
end;

procedure TBase.SetVal;
begin
    fVal := v;
end;

function TSub.Doubled;
begin
    Val := 10;   { protected property, accessed from a descendant - allowed }
    Doubled := Val * 2;
end;

begin
    new(s);
    writeln(s.Doubled);
    dispose(s);
end.
