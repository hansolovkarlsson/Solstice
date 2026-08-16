program TestCMSelfClassM;
type
    TCounter = class
    public
        class var Total: integer;
        class procedure AddToTotal(n: integer);
        class function GetTotal: integer;
    end;

procedure TCounter.AddToTotal;
begin
    { bare access to a class var from inside a CLASS method body - no
      self exists here at all, and no ClassName. qualifier needed }
    Total := Total + n;
end;

function TCounter.GetTotal;
begin
    GetTotal := Total;
end;

begin
    TCounter.AddToTotal(3);
    TCounter.AddToTotal(4);
    writeln('Total = ', TCounter.GetTotal);
end.
