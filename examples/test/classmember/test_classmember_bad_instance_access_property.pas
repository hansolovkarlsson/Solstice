program TestCMBadInstProp;
type
    TCounter = class
    public
        class function GetTotal: integer;
        class property Total: integer read GetTotal;
    end;
var c: TCounter;

function TCounter.GetTotal;
begin
    GetTotal := 1;
end;

begin
    new(c);
    writeln(c.Total);
end.
