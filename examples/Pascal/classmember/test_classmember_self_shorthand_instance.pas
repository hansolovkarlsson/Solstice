program TestCMSelfInst;
type
    TCounter = class
    public
        FVal: integer;
        class var Total: integer;
        class function GetTotal: integer;
        procedure SetVal(n: integer);
        class property TotalProp: integer read GetTotal;
    end;
var c: TCounter;

function TCounter.GetTotal;
begin
    GetTotal := Total;
end;

procedure TCounter.SetVal;
begin
    FVal := n;
    { bare access to a class var and a class property from inside an
      INSTANCE method body - no ClassName. qualifier needed }
    Total := Total + n;
    writeln('TotalProp seen from instance method = ', TotalProp);
end;

begin
    new(c);
    c.SetVal(3);
    c.SetVal(4);
    writeln('FVal = ', c.FVal);
    writeln('Total = ', TCounter.Total);
end.
