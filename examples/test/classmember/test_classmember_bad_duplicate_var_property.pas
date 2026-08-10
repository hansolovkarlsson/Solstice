program TestCMBadDupVarProp;
type
    TCounter = class
    public
        class var Total: integer;
        class function GetTotal: integer;
        class property Total: integer read GetTotal;
    end;

function TCounter.GetTotal;
begin
    GetTotal := 1;
end;

begin
end.
