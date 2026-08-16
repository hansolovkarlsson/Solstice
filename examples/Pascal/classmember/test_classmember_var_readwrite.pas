program TestClassmemberVarReadwrite;
type
    TCounter = class
    public
        class var Total: integer;
    end;

begin
    TCounter.Total := 5;
    writeln('Total = ', TCounter.Total);
    TCounter.Total := TCounter.Total + 10;
    writeln('Total = ', TCounter.Total);
end.
