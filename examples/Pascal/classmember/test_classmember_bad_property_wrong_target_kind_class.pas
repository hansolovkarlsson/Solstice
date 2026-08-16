program TestCMBadPropClassKind;
type
    TCounter = class
    public
        function GetTotal: integer;
        class property Total: integer read GetTotal;
    end;

function TCounter.GetTotal;
begin
    GetTotal := 1;
end;

begin
end.
