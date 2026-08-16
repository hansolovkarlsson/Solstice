program TestCMPropertyRW;
type
    TCounter = class
    public
        class var FTotal: integer;
        class function GetTotal: integer;
        class procedure SetTotal(n: integer);
        class property Total: integer read GetTotal write SetTotal;
        class property RawTotal: integer read FTotal;
    end;

function TCounter.GetTotal;
begin
    GetTotal := FTotal;
end;

procedure TCounter.SetTotal;
begin
    FTotal := n;
end;

begin
    TCounter.Total := 42;
    writeln('Total = ', TCounter.Total);
    writeln('RawTotal = ', TCounter.RawTotal);
end.
