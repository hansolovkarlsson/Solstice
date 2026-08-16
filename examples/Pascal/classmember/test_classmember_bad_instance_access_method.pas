program TestCMBadInstMethod;
type
    TCounter = class
    public
        class function GetTotal: integer;
    end;
var c: TCounter;

function TCounter.GetTotal;
begin
    GetTotal := 1;
end;

begin
    new(c);
    writeln(c.GetTotal);
end.
