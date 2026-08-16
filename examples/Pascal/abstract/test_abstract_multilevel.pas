program TestAbstractMultilevel;
type
    TBase = class
    public
        function Value: integer; abstract;
    end;
    TMiddle = class(TBase)
    public
        function Value: integer; abstract;
        function Describe: string;
    end;
    TLeaf = class(TMiddle)
    private
        FVal: integer;
    public
        function Value: integer;
        procedure SetVal(n: integer);
    end;
var
    leaf: TLeaf;
    base: TBase;

function TMiddle.Describe;
begin
    Describe := 'middle';
end;

function TLeaf.Value;
begin
    Value := FVal;
end;

procedure TLeaf.SetVal;
begin
    FVal := n;
end;

begin
    new(leaf);
    leaf.SetVal(42);
    { TMiddle re-declares 'abstract' again, still deferring - TLeaf is
      the first concrete implementation, 2 levels down }
    base := leaf;
    writeln('Value = ', base.Value);
end.
