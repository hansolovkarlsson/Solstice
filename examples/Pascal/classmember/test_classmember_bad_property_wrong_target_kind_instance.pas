program TestCMBadPropInstKind;
type
    TCounter = class
    public
        class function GetTotal: integer;
        property Total: integer read GetTotal;
    end;

function TCounter.GetTotal;
begin
    GetTotal := 1;
end;

begin
end.
