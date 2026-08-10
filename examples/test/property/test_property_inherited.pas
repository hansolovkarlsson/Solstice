program TestPropertyInherited;
type
    TBase = class
    private
        FVal: integer;
        procedure SetVal(v: integer);
    public
        property Val: integer read FVal write SetVal;
    end;
    TChild = class(TBase)
    public
        function DoubledVal: integer;
    end;
var
    c: TChild;

procedure TBase.SetVal;
begin
    FVal := v;
end;

function TChild.DoubledVal;
begin
    { self-shorthand read of an INHERITED property from a subclass method }
    DoubledVal := Val * 2;
end;

begin
    new(c);
    c.Val := 21;
    writeln('Val = ', c.Val);
    writeln('DoubledVal = ', c.DoubledVal);
    dispose(c);
end.
